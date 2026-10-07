# The code playground

Left: a tiny program. Right: the live state of the runtime — every **variable**, every **object** and its fields,
the heap, the object graph, and each garbage collection in slow motion. Source: `ui/script.js` (compiler),
`ui/playground.js` (editor + executor), `ui/examples.js`.

## 1. Concept
You write a few lines of a toy language. Each line is turned into ordinary runtime operations (allocate, store a
reference, register a root, collect), executed on the real runtime, and the UI shows the result.
**A variable is a GC root** — a slot on a (simulated) thread stack. An object is reachable if some variable
can get to it by following fields.

## 2. The language

```
use generational            pick the collector (rc, marksweep, compact, copying, generational)
a = new Node(next, other)   allocate an object with two reference fields; a points to it
b = new Node                same type again: reuses the field layout
c = new Leaf()              an object with no reference fields
d = new Blob(next)[16]      one field plus 16 bytes of payload
a.next = b                  store a reference   (runs the reference-count / write-barrier hook)
x = a.next                  load a reference into a variable
b = null                    drop a variable (its object loses one root)
a.next = null               clear a field
y = x                       a second variable for the same object
gc()   gc(minor)   gc(major)
repeat 5 { ... }            unrolled loop (1-500 times)
// comment   or   # comment
```

* The **first** `new Type(...)` declares the type's fields; later uses must match or omit them (`new Type`).
  With no fields given the default is `(next, other)`.
* Objects are named after the variable that created them (`a`, then `a_2`, …) so they are easy to follow.
* Objects are small on purpose: a `Node` with 2 fields is 32 bytes in a 2 KB heap (64 objects). Allocation failure
  triggers a GC automatically, as in a real runtime.

## 3. "Compiling"
Every edit is compiled immediately; errors appear under the editor with a line number (`✗ line 3: Variable 'b' is
used before it was given a value`). Checks: syntax, undefined variables, unknown fields, redefined types, unclosed
`repeat`. Runtime errors (e.g. using a variable that is `null`) stop the program and mark the line red.

## 4. Running
| Button | What it does |
|---|---|
| **Step** | run the next line; if a GC is in slow motion, run its next step |
| **Play** | run everything automatically, animating every GC (speed slider) |
| **Run instantly** | run the whole program without animation and show the end state |
| **Stop** / **Restart** | pause Play / empty the heap and go back to line 1 |

The blue box under the editor says which line is next *and what it will do in plain English*; the orange
**What is happening, and why** panel says what just happened.

## 5. What you see on the right
* **Variables & objects** — variables → object, address, status; every object with its named fields, which variables
  hold it, its address/region, age or reference count, and **REACHABLE / UNREACHABLE** (“no variable can reach it”).
* **Heap** — blocks to scale, free space hatched, per-region fragmentation. **Object graph** — roots, arrows, a band
  for unreachable objects. **Timeline + narration** — current GC phase and a one-sentence description of it.
* **Algorithm state** — reference counts and cycle alert (RC), write barrier / remembered set / cards (generational).

## 6. Examples (dropdown)
1. Basic garbage · 2. Cycle (RC vs tracing) · 3. Linked list: drop the head · 4. Shared object (tree) ·
5. Compaction · 6. Copying · 7. Generational aging and promotion · 8. Write barrier & remembered set ·
9. Allocation churn (automatic GC) · 10. Empty sandbox.

## 7. Tests
`node ui/dev/test-script.js` — every shipped example compiles; statement kinds; loop unrolling; error messages.
The C++ side is covered by the `session.*` demos (the same scenarios).

## 8. How to run it
Start `minigc --serve`, open http://127.0.0.1:8080/, choose an example and press **Step**.
