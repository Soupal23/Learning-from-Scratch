# Crypto Library

To understand `createHash`, `update`, and `digest`, we need to look beneath JavaScript. 

Node.js’s `crypto` module is not written in JavaScript. It is a thin JavaScript wrapper around a **C++ layer (`node_crypto.cc`)**, which in turn talks directly to **OpenSSL’s native C engine (`libcrypto`)**.

When you write:
```javascript
const hash = crypto.createHash('sha256').update(chunkText).digest('hex');
```
You are orchestrating a stateful, hardware-accelerated **Merkle–Damgård cryptographic pipeline**.

---

```
                                NODE.JS TO OPENSSL ARCHITECTURE
                                
   JavaScript Layer:       crypto.createHash('sha256')        .update(chunkText)           .digest('hex')
                                      │                               │                           │
                                      ▼                               ▼                           ▼
   C++ Binding Layer:       Hash::New()                      Hash::Update()              Hash::Digest()
   (node_crypto.cc)                   │                               │                           │
                                      ▼                               ▼                           ▼
   Native OpenSSL:          EVP_DigestInit_ex()             EVP_DigestUpdate()          EVP_DigestFinal_ex()
   (libcrypto in C)                   │                               │                           │
                                      ▼                               ▼                           ▼
                         [Allocates Context CTX]          [64-Round Block Loop]       [Bit-Padding & Finalize]
                         [Loads Prime Constants]          [512-bit Buffer Feed]       [Hex String Conversion]
```

---

## 1. `crypto.createHash('sha256')` — Context Initialization

### What happens in OpenSSL:
OpenSSL calls `EVP_MD_CTX_new()` and `EVP_DigestInit_ex()`. It allocates a **Message Digest Context (`EVP_MD_CTX`)** in native C heap memory and loads the fundamental mathematical constants of SHA-256.

#### A. Initial State Registers ($H_0$ to $H_7$)
SHA-256 maintains its running state in **eight 32-bit registers** (totaling 256 bits). 
OpenSSL initializes these registers with eight fixed hex numbers:
```c
H[0] = 0x6a09e667;
H[1] = 0xbb67ae85;
H[2] = 0x3c6ef372;
H[3] = 0xa54ff53a;
H[4] = 0x510e527f;
H[5] = 0x9b05688c;
H[6] = 0x1f83d9ab;
H[7] = 0x5be0cd19;
```
> **Where do these numbers come from?** 
> They are the first 32 bits of the fractional parts of the **square roots of the first 8 prime numbers** ($2, 3, 5, 7, 11, 13, 17, 19$). Cryptographers call these *"nothing-up-my-sleeve numbers"*—proof that the algorithm contains no hidden backdoors.

#### B. The Round Constants ($K_0$ to $K_{63}$)
OpenSSL also loads 64 constant words, derived from the **cube roots of the first 64 prime numbers**. These constants are used to introduce non-linear chaos during each round of hashing.

#### C. The Accumulator Buffer (The 64-byte staging area)
OpenSSL creates a small internal **64-byte (512-bit) staging buffer** and a counter tracking the total number of bytes processed so far.

---

## 2. `.update(data)` — Streaming Block Processing

This is where the actual cryptographic work happens.

SHA-256 is built on the **Merkle–Damgård construction**: it cannot process an arbitrary number of bytes directly. It can **only** process data in rigid, uniform blocks of **exactly 64 bytes (512 bits)**.

```
STREAMING DATA INTO 64-BYTE BLOCKS:

Incoming Text: "Dynamic Programming..." (e.g. 150 bytes)
 ┌───────────────────────┬───────────────────────┬──────────────┐
 │   Block 1 (64 bytes)  │   Block 2 (64 bytes)  │ Remainder    │
 │       Processed       │       Processed       │ (22 bytes)   │
 │      Immediately      │      Immediately      │ Sits in RAM  │
 └───────────────────────┴───────────────────────┴──────────────┘
```

### What happens inside `.update()`:
1. **Buffer Filling:** If you pass data smaller than 64 bytes, OpenSSL copies it into its 64-byte staging buffer and returns immediately. Nothing is hashed yet!
2. **Block Execution:** As soon as the staging buffer reaches 64 bytes, OpenSSL triggers the **SHA-256 Compression Function**:

#### Inside the 64-Round Compression Function:
Every 64-byte block undergoes 64 intense mathematical rounds:
1. **Message Expansion:** 
   The 64 bytes (sixteen 32-bit words $W_0 \dots W_{15}$) are expanded into **sixty-four 32-bit words ($W_0 \dots W_{63}$)** using bitwise rotations (`ROTR`), right shifts (`SHR`), and `XOR` logic:
   $$W_t = \sigma_1(W_{t-2}) + W_{t-7} + \sigma_0(W_{t-15}) + W_{t-16}$$
2. **The Round Mixing Loop (64 iterations):**
   The eight state registers ($a, b, c, d, e, f, g, h$) are scrambled together with the expanded message word $W_t$ and the round constant $K_t$:
   ```
   T1 = h + Σ1(e) + Ch(e, f, g) + Kt + Wt
   T2 = Σ0(a) + Maj(a, b, c)
   h = g
   g = f
   f = e
   e = d + T1
   d = c
   c = b
   b = a
   a = T1 + T2
   ```
   * `Ch(e, f, g)` ("Choose"): If bit in $e$ is 1, pick $f$, else pick $g$.
   * `Maj(a, b, c)` ("Majority"): Returns the majority bit among $a, b, c$.
   * `Σ` (Sigma): Massive bitwise circular rotations.
3. **State Accumulation:** 
   After round 64, the new values are added modulo $2^{32}$ back into our initial state registers:
   $$H_0 = H_0 + a, \quad H_1 = H_1 + b, \quad \dots, \quad H_7 = H_7 + h$$

#### Why Streaming Matters:
Because `.update()` works block by block, **you can hash a 10 GB video or syllabus without loading 10 GB into RAM**:
```javascript
readStream.on('data', chunk => hash.update(chunk));
```
OpenSSL crunches 64 bytes at a time and discards them from RAM, keeping only the eight 32-bit registers ($H_0 \dots H_7$).

---

## 3. `.digest('hex')` — Padding, Finalization & Output

When you call `.digest()`, your data stream is finished. But what happens if your message wasn’t an exact multiple of 64 bytes? (Almost no text ever is!)

### Step 1: Merkle–Damgård Mandatory Padding
OpenSSL applies cryptographic padding so the total message length cleanly fits into 512-bit blocks:
1. It appends a single **`1` bit** (`0x80` in hex) right after the end of your text.
2. It appends **`0` bits** until the block has exactly **56 bytes (448 bits)**.
3. In the final **8 bytes (64 bits)**, it encodes a big-endian 64-bit integer representing the **exact length of the original message**.

```
FINAL PADDED BLOCK (64 bytes / 512 bits):
┌─────────────────────────┬──────────────┬───────────────────────────────┐
│ Remaining Text (e.g. 22B)│ 1 Bit (0x80) │ 00 00 00... │ Original Length │
│                         │              │             │ (64-bit Int)    │
└─────────────────────────┴──────────────┴─────────────┴───────────────┘
                                                       ▲
                                            Exactly byte 56 to 64
```

### Step 2: Final Block Compression
OpenSSL passes this padded block through the 64-round compression function one last time.

### Step 3: Extracting the Digest
The eight state registers ($H_0 \dots H_7$), which have now absorbed all the rounds and the length padding, are concatenated together:
$$8 \text{ words} \times 4 \text{ bytes each} = \mathbf{32 \text{ bytes (256 bits)}}$$

### Step 4: Formatting (`'hex'`)
If you specify `'hex'`, Node.js reads those 32 raw bytes and maps every byte to two hexadecimal characters ($0\text{–}9, a\text{–}f$):
$$32 \text{ bytes} \times 2 = \mathbf{64 \text{ hexadecimal characters}}$$
*(e.g., `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`)*

### Step 5: Native Context Teardown
OpenSSL immediately calls `EVP_MD_CTX_free()`, zeroing out and freeing the memory allocated in C++.
> **Why you cannot call `.update()` or `.digest()` twice:**
> Once `.digest()` executes, the internal OpenSSL context pointer is destroyed. If you call `.digest()` again, Node.js throws: `Error: Digest already called`.

---

## 4. Why This Architecture Powers AlgoPrep Phase 3

In your AlgoPrep codebase, this exact pattern appears in two critical places:

### 1. In `server/src/parsing/chunker.js` (Content Deduplication)
```javascript
const hash = crypto.createHash('sha256').update(rawText).digest('hex');
if (!seenHashes.has(hash)) {
  seenHashes.add(hash);
  chunks.push({ text: rawText, hash, ... });
}
```
* **The Avalanche Effect:** If a student uploads notes where two paragraphs differ by just a single comma, SHA-256 flips ~50% of the bits completely. 
* Deduplicating via a 64-character hash is instantaneous ($O(1)$ lookup in a `Set`), preventing redundant chunks from clogging your MongoDB database and saving vector embedding costs.

### 2. In `server/src/ai/embedder.js` (In-Memory Embedding Cache)
```javascript
hash(text) {
  return crypto.createHash('sha256').update(text).digest('hex');
}
```
* Instead of storing multi-kilobyte text passages as keys in `this.cache = new Map()`, AlgoPrep stores the 64-byte hash.
* String comparisons in memory drop from comparing 2,000 characters down to comparing a 64-byte key, making cache lookups lightning fast before calling Google Gemini’s API.

# What do we use it in ?

Ran search for createHash in server

In AlgoPrep, you are using cryptographic hashing (`SHA-256`) for **Content-Addressable Fingerprinting**. 

Specifically:

---

### 1. WHAT Are We Hashing?

We are hashing the **raw text content of every document chunk**.

When a student uploads a 50-page PDF on *"Dynamic Programming"*, the parser extracts the text, and the chunker slices it into paragraphs of ~500–800 tokens. 

Take a chunk like this:
```text
"Dynamic programming is both a mathematical optimization method and an 
algorithmic paradigm. It simplifies a complicated problem by breaking it 
down into simpler sub-problems in a recursive manner..."
```

AlgoPrep feeds that exact string into SHA-256 and turns it into a unique, fixed-length fingerprint:
```
Text (1,500 characters) ──► SHA-256 ──► "e8b2489c67...31a9" (64 hex characters)
```

---

### 2. FOR WHAT PURPOSE? (The 3 Crucial Reasons in AlgoPrep)

You use this hash in two files: `server/src/parsing/chunker.js` and `server/src/ai/embedder.js`. Here is why:

---

#### Reason 1: Preventing Duplicate Chunks (`chunker.js`)
* **The Reality of Study Materials:**
  * Professors repeat the same "Overview" or "Course Schedule" slide in 15 different lecture decks.
  * Authors repeat summary boxes, copyright notices, or introductory paragraphs across chapters.
  * When chunking long paragraphs using a sliding window, overlap boundaries can accidentally generate identical or near-identical text blocks.
* **How hashing solves it:**
  In `chunker.js`, AlgoPrep tracks:
  ```javascript
  const seenHashes = new Set();
  
  const hash = crypto.createHash('sha256').update(rawText).digest('hex');
  if (!seenHashes.has(hash)) {
    seenHashes.add(hash);
    chunks.push({ text: rawText, hash, ... });
  }
  ```
  If an identical paragraph appears on Slide 3 and Slide 45, the hash will match **100% identically**. AlgoPrep discards the duplicate instantly before it ever touches your database or AI models.

---

#### Reason 2: Saving API Costs & Quota on Gemini (`embedder.js`)
Generating vector embeddings using Google’s `text-embedding-004` model is **not free**:
* It consumes your API rate limits (tokens per minute / requests per minute).
* It introduces network latency (150ms–400ms per batch).

In `embedder.js`, you have an in-memory cache:
```javascript
class Embedder {
  constructor() {
    this.cache = new Map(); // hash -> [0.042, -0.118, 0.891, ...]
  }

  hash(text) {
    return crypto.createHash('sha256').update(text).digest('hex');
  }
```

* **Before calling Google Gemini:** 
  The embedder computes the hash of the text chunk. If that hash already exists in `this.cache`, it **skips the Gemini API network call entirely** and pulls the 768-dimensional vector straight out of RAM in **0.001 milliseconds**.
* **Why not use the raw text as the Map key?**
  A chunk is ~2,000 characters long with spaces, newlines, and unicode. Storing 2,000 raw strings in a JavaScript `Map` consumes significant RAM. Storing a compact, fixed 64-byte hash takes a fraction of the memory and allows instantaneous hash lookup.

---

#### Reason 3: Ultra-Fast MongoDB Indexing (`Chunk.js`)
In `server/src/models/Chunk.js`, you define:
```javascript
chunkSchema.index({ workspaceId: 1, hash: 1 });
```

* In MongoDB, B-Tree indexes are fast and efficient when indexing **small, fixed-size values** (like 64-character hash strings).
* If you tried to index the raw `text` field (which can be 2,000–3,000 characters), MongoDB’s index would bloat dramatically, exceed memory limits, and slow down your queries.
* With `{ workspaceId: 1, hash: 1 }`, MongoDB can tell in **sub-millisecond B-Tree time** whether a workspace already has a specific chunk.

---

### Summary Table

| Where It's Used | What Is Being Hashed | Why We Hash It | The Concrete Benefit |
| :--- | :--- | :--- | :--- |
| **`chunker.js`** | Text of each paragraph/section | To detect duplicate content across slides/pages | Stops duplicate text from polluting search results and vector storage. |
| **`embedder.js`** | Text sent to Gemini API | Key for the in-memory cache (`Map`) | Saves Gemini API costs, preserves rate limits, and speeds up ingestion from minutes to seconds. |
| **`Chunk.js` (MongoDB)** | Chunk content fingerprint | Unique compound index in B-Tree | Keeps database index sizes tiny and lookups fast. |

Analyzed embedder.js:L38-117

**Yes, exactly! That is one of its primary jobs.**

The `Map` inside `embedder.js` acts as an **in-memory Cache** that prevents your server from re-embedding the same text twice.

Here is the exact code logic and a step-by-step walkthrough of what happens when a student uploads a document.

---

### The Code in `server/src/ai/embedder.js`

In `embedder.js`, you have an instance variable:
```javascript
this.cache = new Map(); // Key: 64-char Hash  -->  Value: [0.042, -0.118, ... 768 floats]
this.maxCacheSize = 2000;
```

When a document is sliced into an array of paragraphs (say, 50 paragraphs), the method `embedBatch(texts)` executes:

```javascript
const results = new Array(texts.length);
const toFetchIndices = [];
const toFetchTexts = [];

// 1. CHECK THE MAP FIRST
texts.forEach((txt, idx) => {
  const h = this.hash(txt);
  if (this.cache.has(h)) {
    // CACHE HIT: Grab it instantly from RAM!
    results[idx] = this.cache.get(h);
  } else {
    // CACHE MISS: We must ask Google Gemini for this one
    toFetchIndices.push(idx);
    toFetchTexts.push(txt);
  }
});
```

---

### Step-by-Step Scenario: A Student Uploads Notes

Imagine a student uploads a 10-page document containing **50 paragraphs**:

#### Step 1: The Hash Check
The embedder loops through all 50 paragraphs and computes their 64-character SHA-256 hash.
* Paragraphs #1 to #40 are brand-new text. Their hashes are **not** in `this.cache`.
* Paragraphs #41 to #50 are repeated sections (for example: course disclaimers, repeated introduction paragraphs, or sections previously processed in another file). Their hashes **are** already in `this.cache`.

#### Step 2: Slicing the Workload
* `results[40]` through `results[49]` are **immediately populated from the `Map` in 0.001 ms**.
* `toFetchTexts` now only contains the **40 new paragraphs** (instead of 50).

#### Step 3: Sending Only the Missing Paragraphs to Gemini
Only the 40 missing paragraphs are sent over the internet to Google’s Gemini API in batches of 32:
```javascript
ai.models.embedContent({ ... })
```
> **You just saved 10 API calls, reduced token consumption, and made the upload faster.**

#### Step 4: Storing the New Vectors for Future Use
When Gemini returns the 40 new vectors, your code stores them in the `Map` for next time:
```javascript
// Evict oldest item if cache reaches 2,000 items (FIFO)
if (this.cache.size >= this.maxCacheSize) {
  const firstKey = this.cache.keys().next().value;
  this.cache.delete(firstKey);
}

// Save the new hash and vector into the Map
this.cache.set(this.hash(chunk[resIdx]), vector);
```

---

### Why Have Both `seenHashes` in `chunker.js` AND `this.cache` in `embedder.js`?

You might wonder: *"Wait, doesn't `chunker.js` already deduplicate?"*

They protect different boundaries:

| Mechanism | Where It Lives | Scope | Purpose |
| :--- | :--- | :--- | :--- |
| **`seenHashes` (in `chunker.js`)** | Temporary local `Set` | **Single Document** | Prevents duplicate chunks from being saved into the MongoDB database for that file. |
| **`this.cache` (in `embedder.js`)** | Long-lived server `Map` | **Global Across the Entire Server** | Persists in RAM across **multiple files, multiple workspaces, and different users**. |

#### Concrete Example:
1. **Student A** in Workspace 1 uploads `Intro_Algorithms.pdf` containing a standard section on *"Time Complexity"*. Gemini embeds it and saves the hash $\to$ vector in `this.cache`.
2. 5 minutes later, **Student B** in Workspace 2 (or Student A uploading a different file) uploads `Week1_Notes.pdf` containing the exact same *"Time Complexity"* text.
3. `chunker.js` won't catch it because it's a completely new file and new workspace.
4. **`embedder.js` catches it!** It finds the hash in `this.cache` and reuses the vector instantly without hitting the Gemini API at all.