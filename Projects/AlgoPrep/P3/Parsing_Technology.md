# How do text paresers work ?

To understand how text parsers work at their core, you have to discard a common misconception:

> **A document file is NOT just text with styling tags.**

If you open a `.pdf`, `.docx`, or `.pptx` with a plain text reader, you get binary garbage. Each format was engineered with a completely different mental model:
* **PDFs** were designed for **printers and displays** (a 2D canvas with drawing instructions).
* **DOCX / PPTX** were designed as **zipped packages of relational XML databases** (semantic markup).

Let’s tear them open and explore how `unpdf`, `pdf-parse`, and `mammoth` work down to the byte and data-structure level.

---

```
                       HOW DOCUMENT PARSERS WORK INTERNALLY

 ┌───────────────┐
 │   PDF File    │ ──► Byte Scanner ──► XREF Table ──► FlateDecode (zlib) ──► Canvas Operator Engine
 └───────────────┘                                                             (Reconstructs coordinates,
                                                                                glyphs, spaces & lines)
 ┌───────────────┐
 │  DOCX / PPTX  │ ──► Unzip Archive ──► DOM / SAX Parser ──► Style Resolver ──► Semantic AST ──► Markdown
 └───────────────┘     (Memory zip)      (word/document.xml)   (word/styles.xml)   (<w:p>, <w:r>, <w:t>)
```

---

## 1. Deep Dive: PDF Parsing (`pdf-parse` & `unpdf`)

### A. The PDF File Architecture (PostScript Heritage)
A PDF is not read top-to-bottom like a script. It is an **object-graph database** accessed randomly via byte offsets:

```
%PDF-1.7                                 <-- 1. Header (PDF Version)
1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj
2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj
3 0 obj << /Type /Page /Contents 4 0 R >> endobj
4 0 obj                                  <-- 2. Content Stream (The Page Canvas)
<< /Length 124 /Filter /FlateDecode >>
stream
... (compressed binary zlib stream) ...
endstream
endobj
xref                                     <-- 3. Cross-Reference (XREF) Table
0 5
0000000000 65535 f 
0000000009 00000 n                       <-- Byte offset of Object 1
0000000062 00000 n                       <-- Byte offset of Object 2
trailer << /Root 1 0 R >>                <-- 4. Trailer (Entry point to the graph)
startxref
450                                      <-- Byte offset where XREF starts
%%EOF
```

#### How the parser reads this:
1. **The parser reads the file from the *bottom up*.** It looks for `%%EOF` and reads the byte offset under `startxref`.
2. It jumps directly to the **XREF table**, which is an index of every object’s exact byte position in the file.
3. It resolves the **Trailer $\to$ Root Catalog $\to$ Pages Tree $\to$ Page Objects**.
4. It reads the page's `/Contents` stream and runs it through a `zlib / FlateDecode` decompression algorithm to get the raw drawing commands.

---

### B. The Secret of PDF "Text": It's Actually Paint on a Canvas

Once decompressed, what does the page text look like? 
**There are no sentences, no paragraphs, and often NO space characters!**

Instead, the stream contains an assembly-like set of **PostScript graphics instructions**:

```postscript
BT                   % Begin Text object
/F1 12.00 Tf         % Use Font F1 at size 12
1 0 0 1 72.0 710.0 Tm % Place "pen" at coordinate X=72.0, Y=710.0
(AlgoPrep) Tj        % Paint glyphs "AlgoPrep"
120.5 0 Td           % Move pen X by 120.5 points
[(V2) 15 (Phase) 20 (3)] TJ  % Paint array with coordinate spacing!
ET                   % End Text object
```

Look closely at `[(V2) 15 (Phase) 20 (3)] TJ`:
* The PDF did **not** write `"V2 Phase 3"`.
* It painted `"V2"`, shifted the virtual print-head by `15` units of space, painted `"Phase"`, shifted by `20` units, and painted `"3"`.
* **In many PDFs, the spacebar character (`0x20`) is never stored.** The authoring software simply drew glyphs with horizontal gaps between them.

---

### C. The Parser's Algorithmic Challenge: Reconstructing Language

To extract human-readable text, the parser must run a **reverse-rendering engine**:

#### 1. The Space Detection Algorithm
The parser looks at two consecutive text fragments:
$$\Delta X = X_{\text{next}} - (X_{\text{prev}} + \text{Width}_{\text{prev}})$$
If $\Delta X > 0.3 \times \text{FontSize}$, the parser assumes: *"A human intended there to be a space here"*, and synthesizes a `' '` character.

#### 2. The Line & Reading Order Algorithm
Because text in a PDF stream can be placed in **any random order** (e.g., footers drawn first, then headers, then body text):
* The parser extracts an array of text tokens: `{ text: "...", x: 72, y: 710, width: 45 }`.
* It runs a **2D Geometric Sort**:
  1. Primary sort: **$Y$ coordinate descending** (top of page to bottom of page).
  2. If two tokens have nearly identical $Y$ coordinates ($|Y_1 - Y_2| < \text{threshold}$), it applies a secondary sort: **$X$ coordinate ascending** (left to right).
* This geometric clustering is what groups words into lines and lines into paragraphs.

---

### D. `pdf-parse` vs. `unpdf`: Why AlgoPrep Chose `unpdf`

| Feature | `pdf-parse` (Legacy) | `unpdf` (Modern / AlgoPrep Primary) |
| :--- | :--- | :--- |
| **Foundation** | Ancient wrapper around PDF.js v1. | Modern wrapper around Mozilla's modern `pdfjs-dist`. |
| **Output Format** | A single concatenated string of the entire document. | Can return an array of pages: `[{ page: 1, text }, { page: 2, text }]`. |
| **Citation Support** | **Fails citations:** You cannot reliably tell where Page 1 ended and Page 2 began. | **Preserves citations:** It allows `{ mergePages: false }`, giving page-accurate chunks. |
| **Runtime Portability** | Relies on Node.js native shims and `Canvas` polyfills (crashes in edge/serverless). | Headless, zero-native dependencies, runs in Node, Bun, and edge environments. |

---

## 2. Deep Dive: DOCX Parsing (`mammoth`)

A Microsoft Word `.docx` file is completely different from a PDF. A `.docx` file is a standard **ZIP archive** containing an Open Packaging Convention (OOXML) file system.

If you rename `syllabus.docx` to `syllabus.zip` and extract it, you get:
```
syllabus.zip
├── [Content_Types].xml       <-- File format registry
├── _rels/.rels               <-- Root relationships
├── word/
│   ├── document.xml          <-- THE ACTUAL TEXT AND STRUCTURE
│   ├── styles.xml            <-- Heading definitions, fonts, sizes
│   └── numbering.xml         <-- Bulleted and numbered list trees
```

### A. The XML Hierarchy (`word/document.xml`)
Word stores text in a strict XML schema called **WordprocessingML**:

```xml
<w:document>
  <w:body>
    <!-- Paragraph 1: A Heading -->
    <w:p>
      <w:pPr>
        <w:pStyle w:val="Heading1"/>  <!-- Style ID referencing styles.xml -->
      </w:pPr>
      <w:r>
        <w:t>Dynamic Programming</w:t>
      </w:r>
    </w:p>

    <!-- Paragraph 2: Body text with mixed formatting -->
    <w:p>
      <w:r>
        <w:t>This is </w:t>
      </w:r>
      <w:r>
        <w:rPr><w:b/></w:rPr>         <!-- Bold formatting -->
        <w:t>crucial</w:t>
      </w:r>
      <w:r>
        <w:t> for coding tests.</w:t>
      </w:r>
    </w:p>
  </w:body>
</w:document>
```

#### Notice the `<w:r>` tag (A "Run"):
In Word, text is segmented into **Runs**. A Run is a contiguous string of characters that share the exact same formatting. 
In the second paragraph, the sentence *"This is crucial for coding tests"* is split into **three separate runs** because the middle word is bold!

---

### B. Mammoth’s Philosophy: Semantic Conversion (Not Presentation)

Most converters try to make the output *look* visually identical to Word (translating 24pt Calibri to CSS). 

**Mammoth was designed specifically for documents going into web apps and AI pipelines.** It operates on **semantic intent**:
1. **Unzip & Stream Parse:** Mammoth opens the file buffer in memory and streams `word/document.xml` through an XML parser.
2. **Style Cross-Referencing:**
   When it encounters `<w:pStyle w:val="Heading1"/>`, it cross-references `word/styles.xml`. It realizes this paragraph is semantically a level-1 header.
3. **Run Collapsing:**
   Instead of keeping separate text runs, Mammoth concatenates `<w:t>` elements inside a paragraph, applying semantic Markdown:
   * Plain text + bold run + plain text $\longrightarrow$ `This is **crucial** for coding tests.`
4. **List Resolution:**
   Using `word/numbering.xml`, it traces indentation levels and emits clean Markdown bullets (`* ` or `1. `).
5. **No Visual Bloat:** Mammoth discards page margins, custom colors, line-spacing, and font faces. You get clean, dense text that is optimal for LLM tokenization.

---

## 3. Deep Dive: PPTX Parsing (`officeparser`)

Just like `.docx`, a PowerPoint `.pptx` file is a ZIP archive containing **PresentationML**.

Inside the zip, every slide is its own independent XML file:
```
presentation.zip
├── ppt/
│   ├── presentation.xml
│   └── slides/
│       ├── slide1.xml        <-- Slide 1 Text & Shapes
│       ├── slide2.xml        <-- Slide 2 Text & Shapes
│       └── slide3.xml
```

### How `officeparser` works internally:
1. It unzips the archive into memory.
2. It looks inside the `ppt/slides/` directory and discovers `slide1.xml`, `slide2.xml`, etc.
3. In PowerPoint, text lives inside **DrawingML shape elements**:
   ```xml
   <p:sp>                     <!-- Shape -->
     <p:txBody>               <!-- Text Body -->
       <a:p>                  <!-- Paragraph -->
         <a:r>                <!-- Run -->
           <a:t>Slide Title</a:t>
         </a:r>
       </a:p>
     </p:txBody>
   </p:sp>
   ```
4. `officeparser` traverses each slide XML, extracts the `<a:t>` tags in visual order, and tags them with slide delimiters.
5. In AlgoPrep (`server/src/parsing/index.js`), the code takes this output and splits it using regex:
   ```javascript
   const rawSlides = cleaned.split(/(?:---+|Slide \d+:?|\n\n\n+)/i);
   ```
   Each slice is tagged with `{ slide: idx + 1, text }`, preserving the exact presentation slide number for citations!

---

## 4. The Post-Parsing Pipeline: Cleaning (`cleaner.js`)

Raw text from all parsers is dirty. Before text can be passed to the chunker and the Gemini embedder, AlgoPrep runs it through `cleanText()`:

1. **Hyphenation Repair across Line Breaks:**
   When documents wrap words at margin edges, you get:
   `"This is a backpropaga-\ntion algorithm"`
   A regex fixes this: `text.replace(/(\w+)-\n(\w+)/g, '$1$2')` $\to$ `"backpropagation"`.
2. **Unicode Ligature Normalization:**
   Printers combine letter pairs into single glyphs for typography:
   * `\uFB01` $\to$ `fi`
   * `\uFB02` $\to$ `fl`
   * `\uFB03` $\to$ `ffi`
   Normalization converts these into standard ASCII characters so vector embeddings don't miss them.
3. **Repeated Header/Footer Removal:**
   If a PDF has *"Introduction to Algorithms - Page 12"* at the top of every single page, keeping it would distort RAG search results. Cleaners detect and strip repeated top/bottom edge lines.

---

### Technical Summary

| Format | Native Structure | Parser | How It Reconstructs Text | What It Outputs to AlgoPrep |
| :--- | :--- | :--- | :--- | :--- |
| **PDF** | PostScript 2D canvas drawing commands. | `unpdf` (fallback: `pdf-parse`) | Evaluates coordinates $(X,Y)$, synthesizes missing spaces, sorts tokens into lines. | Array of page sections: `[{ page: 1, text }, { page: 2, text }]` |
| **DOCX** | Zipped OOXML (`word/document.xml`). | `mammoth` | SAX XML parsing, maps styles to semantic Markdown headers, ignores layout noise. | Paragraph sections with headers intact. |
| **PPTX** | Zipped PresentationML (`ppt/slides/slideN.xml`). | `officeparser` | Traverses DrawingML `<p:txBody>` shapes per slide file. | Array of slide sections: `[{ slide: 1, text }, { slide: 2, text }]` |