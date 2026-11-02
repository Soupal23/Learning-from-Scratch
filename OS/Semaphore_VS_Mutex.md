## Question:
Can you explain this in more depth?

Assuming Mutex and Binary Semaphore Are Always Identical
A common mistake is saying that a mutex and a binary semaphore are always the same because both can involve values 0 and 1.

Similar values do not mean the same behavior. A binary semaphore may look like a mutex in a same-thread wait-and-signal pattern, but it can also be waited by one thread and signaled by another.

i want to know how the system works in a semaphore one thread T1 does wait(s) binary semaphore goes from 1->0 another thread T2 can call signal(s) it goes from 0->1, but for T2 to call signal(s) it first has to get the permit using wait(s), but T1 does that before T2 so how does this line make sense "but it can also be waited by one thread and signaled by another."

## Answer:

Your confusion is exactly where most people get stuck, because you are applying mutex rules to a semaphore.

The short answer is: **a thread does NOT need to own a permit to signal a semaphore.** Any thread can signal at any time.

Let's break how the system actually works.

### 1. Signal is not Unlock

With a mutex, the rule is:

> Whoever locks, must unlock. Unlock without lock = error.

With a semaphore, there is no owner. The rules are only about the counter `S`:

* `wait(S)`: If `S > 0`, do `S = S - 1` and continue. If `S == 0`, block and go to the wait queue.
* `signal(S)`: Do `S = S + 1` (for binary semaphore, capped at 1) and wake up one waiting thread if any.

Notice: `signal` never checks "did this thread call wait before?" The OS does not track ownership for semaphores.

So T2 does not have to call `wait` to earn the right to call `signal`. Calling `signal` *creates* the permit.

### 2. The case you described: T1 waits, T2 signals

You imagined:

> T1 does wait, S: 1 -> 0. Now how can T2 signal if it never did wait?

T2 can, because signal just means 0 -> 1. Walk through it:

Initial: `S = 1`

1. T1 calls `wait(S)`. S is 1, so it succeeds. `S = 0`. T1 enters critical section.
2. T2 now wants to enter, calls `wait(S)`. S is 0, so T2 blocks. It is put to sleep by the OS.
3. T1 finishes, calls `signal(S)`. `S = 0 -> 1`, and OS wakes T2.
4. T2 now wakes, completes its `wait`, does `S = 1 -> 0`, and proceeds.

Here T1 and T2 both did wait and signal in the same thread. This *looks* exactly like a mutex. This is why people say "binary semaphore looks like a mutex."

### 3. The other pattern that a mutex cannot do

Now initialize `S = 0` and use it for signaling, not for mutual exclusion:

Initial: `S = 0`

1. Thread A calls `wait(S)`. S is 0, so A blocks immediately. It did NOT do 1 -> 0, it just sleeps waiting for an event.
2. Thread B does some work, for example: `data = loadFile()`, then calls `signal(S)`. `S = 0 -> 1`, and wakes A.
3. A wakes up, does `S = 1 -> 0`, and now uses `data`, knowing B has finished.

See what happened? **A waited, B signaled.** B never called `wait` at all. That is completely legal for a semaphore.

This is used for ordering and coordination:
Producer produces -> signals. Consumer waits -> consumes.

### 4. Why this would be illegal with a mutex

If you tried the same with a mutex:

1. Mutex starts unlocked.
2. Thread A locks (becomes owner).
3. Thread B tries to unlock while A still owns it to "wake" A.

The OS will reject this, return an error, or cause undefined behavior, because B is not the owner.

That is the core difference from the article:

> Mutex is about **ownership**. Semaphore is about **permits**.

Same values 0 and 1 do not mean same behavior. Ownership is the rule that separates them.

Remember it like this: a mutex is like a house key — only the person who locked the door should unlock it. A semaphore is like a flag — one person can raise it down (wait), a different person can raise it up (signal) to tell others "event happened, you can go."