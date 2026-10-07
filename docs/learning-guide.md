# Learning guide: garbage collection from zero, using HeapLens

This guide takes you from "what is memory?" to "I can explain every part of this project and defend its design".
Each chapter has **ideas**, **a hands-on experiment in the UI**, **where it lives in the code**, and **check yourself**
questions (answers at the end). Budget: about 6–8 hours spread over a few days.

How to work: keep the UI open (`start.bat`, or see [../BUILD.md](../BUILD.md)) in one window and this guide in another.
Experiments name an example from the *Code* tab's dropdown; press **Step** and read the orange card as you go.
Reading order for code is in chapter 12.

---

## Chapter 1 — Memory, and why anyone needs a garbage collector

### 1.1 Two kinds of memory
A running program uses two regions:

* **The stack**: one slot per local variable and function call. It grows and shrinks automatically as functions are
  entered and left. Fast, but the data dies when the function returns.
* **The heap**: a big pool of bytes for data that must outlive one function call (a list, a tree, a cache, a window object).
  Someone has to decide *when a piece of heap memory is no longer needed*.

### 1.2 Who frees heap memory?
* **Manually** (C, C++ `new`/`delete`): the programmer decides. Two classic bugs follow:
  * **Memory leak** — forgetting to free: memory use grows until the machine runs out.
  * **Dangling pointer / use-after-free / double free** — freeing too early or twice: crashes and security holes.
* **Automatically** (Java, C#, Go, Python, JavaScript): a **garbage collector (GC)** finds memory the program can no longer
  reach and frees it. The programmer cannot create dangling pointers, and leaks become much harder.

### 1.3 What must a GC solve?
1. **What is garbage?** (Chapter 2: reachability.)
2. **How do we find it?** (Chapters 4–8: five different strategies.)
3. **How do we reuse the memory efficiently?** (Chapter 3: allocators, fragmentation.)
4. **How do we do it without freezing the program for long?** (Chapters 8–10: generations, barriers, safepoints.)

HeapLens implements **all of these** at a small scale so you can watch them.

**Check yourself:** Q1.

---

## Chapter 2 — Objects, references, roots, reachability

### 2.1 Objects and references
An **object** is a chunk of heap memory holding some data and some **references** (addresses of other objects). Objects
and references form a **graph**: objects are nodes, references are arrows.

```
a ──► [A | next ─► B ] ──► [B | next ─► null]
```

### 2.2 Roots
The program itself can only touch objects through variables: local variables, globals, function arguments. Those live
**outside the heap** (on the stack or in globals). They are the **roots** of the graph.

> In HeapLens's code language, **every variable is a root**: `a = new Node` creates an object and a root `a` pointing to it.

### 2.3 Reachable = alive
An object is **reachable** if there is a path of arrows from some root to it. If there is no such path, the program can
never get to it again: it is **garbage**. This is the *only* definition of garbage the tracing collectors here use.

> It is **not** "old", "big" or "unused for a while". An object created a microsecond ago is garbage if nothing reaches it,
> and an object that has lived for hours is live as long as a path exists.

### 2.4 Experiment — example **1. Basic garbage**
1. Press **Step** until you reach `gc()`; look at the *Variables & objects* table: `c` and `d` are gone as variables, and
   objects C and D show **UNREACHABLE — no variable can reach it**. They are still in memory! Garbage costs space until a GC runs.
2. Click an object: the inspector card shows **why**: `Root 'a' -> a (#1) -> b (#2)` for live objects,
   `No path exists from any GC root to c (#3)` for garbage.
3. Step through the collection and watch the states: UNVISITED → DISCOVERED → SCANNED → LIVE, and garbage → UNREACHABLE → RECLAIMED.

**Where in the code:** `roots.h`, `graph.h` / `graph.cpp` (`ObjectGraph::reachable`, `explain`). The graph is the *ground
truth*: every collector is tested against it on randomly generated graphs (`graph.*_agrees_with_reference`).

**Check yourself:** Q2, Q3.

---

## Chapter 3 — Our simulated machine: bytes, addresses, allocators

### 3.1 The heap is real bytes
HeapLens reserves an actual byte array (`Memory`, 2048 bytes by default) and pretends it starts at address `0x1000`. Objects are
stored in it for real, so moving an object really copies bytes and really changes addresses.

### 3.2 Object layout
```
+---------------------+ <- address
| header (24 bytes)   |  id, size, forwarding addr, refcount, type, colour, age, #refs
+---------------------+
| ref slots, 4 B each |  an address, or 0 for null
+---------------------+
| payload bytes       |
+---------------------+   total rounded up to a multiple of 8
```
`Node(next, other)` = 24 + 2×4 = **32 bytes**, so a 2 KB heap holds 64 of them. A free block has the same first 8 bytes
(`id = 0`, `size`) so we can walk a region **block by block**, objects and holes alike.

### 3.3 Allocation strategies
* **Bump pointer**: keep a cursor; allocate by moving it forward. Super fast; can never fragment; cannot free single objects.
  Used by Eden, the survivor spaces and the copying semispaces.
* **Free list**: keep a list of free blocks. Allocate by **first fit**; if the block is bigger than needed, **split** it;
  when a block is freed, **coalesce** it with free neighbours. Used by the main heap and the old generation.

### 3.4 Fragmentation
Free memory can be plentiful but chopped into small holes, so a bigger request fails.
`fragmentation = 1 − largest_free_block / total_free`.

### 3.5 Experiment — example **5. Compaction** (stop after the first `gc()`)
Run it with Step until the first `gc()` completes. In the **Memory** tab the heap shows `[a][FREE][b][FREE][c][ FREE… ]` and the
fragmentation figure is above 0 %. Allocation of an object bigger than the biggest hole would fail even though total free
space is large. This is exactly the problem compaction and copying solve.

**Where in the code:** `heap.h`/`heap.cpp` (`BumpAllocator`, `FreeListAllocator::allocateBlock/release/rebuildFromHeaders`),
`object.h`. Tests: `allocator.*`.

**Check yourself:** Q4, Q5.

---

## Chapter 4 — Reference counting

### 4.1 The idea
Each object stores *how many references point at it*. Store a reference → +1 on the target. Remove or overwrite one → −1 on the
old target. At **0** the object is freed immediately — and its own outgoing references are removed, which may free more objects.

### 4.2 Pros and cons
* **+** immediate reclamation, no long pauses, simple.
* **−** every pointer store costs extra work; and **cycles are never freed**:

```
a ─► A ─► B ─► A          after dropping variable a:  A.count = 1 (from B), B.count = 1 (from A)  → leak
```
Each is held alive by the *other*, which is unreachable.

### 4.3 Experiment — example **2. Cycle**
Run to the end: reference counts stay at 1, the *Algorithm state* (Details tab) shows an **UNREACHABLE CYCLE** alert.
Change line 2 to `use marksweep`, press **Restart**, run again: both objects are reclaimed. That difference is the whole reason tracing
collectors exist.

**Where in the code:** `gc_refcount.cpp` (`increment`, `decrement`, recursive `reclaim`; `collect()` only *diagnoses* cycles).
Tests: `refcount.*`.

**Check yourself:** Q6.

---

## Chapter 5 — Mark-Sweep (tracing)

### 5.1 Idea
*Don't count — **trace**.* Start at the roots and follow every reference, **marking** everything you reach. Anything unmarked
is garbage; **sweep** the heap and free it.

### 5.2 Tri-colour marking
* **White** — not seen (candidate garbage).
* **Gray** — seen, but its references have not been followed yet (in the worklist).
* **Black** — seen and all its references followed.
Algorithm: roots → gray; while a gray object exists: scan it (follow its references, turning newly found white objects gray),
then mark it black. When no gray is left, **white = garbage**.

### 5.3 Sweep
Walk every block in the heap in address order; unmarked objects become free blocks; adjacent free blocks coalesce. Live objects
**never move**, so holes (fragmentation) remain.

### 5.4 Experiment — example **1** or **4. Shared object**
Step slowly through the collection. Notice:
* the **order** objects turn gray/black (breadth-first),
* the **edge pulse** in the Graph tab showing which reference is being followed,
* in example 4, `shared` is **not** reclaimed after `top.left = null` because `r` still reaches it: liveness is about *any* path.

**Where in the code:** `gc_common.cpp` (`Tracer`), `gc_marksweep.cpp`. Tests: `marksweep.*`.

**Check yourself:** Q7, Q8.

---

## Chapter 6 — Mark-Compact (and why references must be updated)

### 6.1 Idea
Fix fragmentation by **sliding** live objects to one end of the heap. The catch: an object's address changes, so **every
reference to it must change too** — even the reference stored in an object that did not move.

### 6.2 The four passes (Lisp-2 style)
1. **Mark** (as before).
2. **Compute forwarding addresses**: walk the heap; give each live object the next free address from the start. Store it in the
   object's header (`forward`).
3. **Update references**: rewrite every root and every reference field to the target's forwarding address. (Objects have not moved
   yet, so the old headers are still readable.)
4. **Move**: slide each live object to its forwarding address, lowest first (safe, because objects only move *down*).

### 6.3 Experiment — example **5. Compaction**
Run to line 14 (`gc()` after `use compact`) and step through. Click `b` or `c` during **COMPUTE ADDRESSES** (forwarding address in
the inspector), then **UPDATE REFERENCES** (the card shows *old reference → new reference*), then **MOVE OBJECTS** (blocks jump
left). Afterwards the heap is `[a][b][c][ FREE… ]` with one free block.

**Where in the code:** `gc_markcompact.cpp`; helpers `updateRootRef`, `updateFieldRef`, `forwardedAddress` in `gc_common.cpp`.
Tests: `compact.*` (e.g. `compact.references_and_roots_are_updated`).

**Check yourself:** Q9.

---

## Chapter 7 — Copying GC (Cheney's semispaces)

### 7.1 Idea
Split the heap into **from-space** and **to-space**. Allocate in from-space with a bump pointer. To collect, **copy** every
reachable object into the empty to-space, leave a **forwarding pointer** in the old copy, and fix references; then **swap** the
spaces. Garbage is never touched — it is simply left behind.

### 7.2 Cheney's trick
Use the to-space itself as the worklist. Two pointers: `scan` and `free`. Copy the roots (advancing `free`). Then while
`scan < free`: look at the object at `scan`, copy anything it references (advancing `free`), fix its fields, advance `scan`.
When they meet, everything live is copied.

### 7.3 Why it is attractive
* Cost ∝ **live** data, not heap size.
* Survivors are contiguous ⇒ **no fragmentation**, allocation is a pointer bump.
* Cost: half the memory is idle; objects move.

### 7.4 Experiment — example **6. Copying collector**
Watch `FROM-SPACE` and `TO-SPACE` rows. During the collection blocks appear in TO-SPACE one by one; at the end the row names swap
(**SPACES SWAPPED**). A second `gc()` copies them back.

**Where in the code:** `gc_copying.cpp` (`evacuate`, the scan loop). Tests: `copying.*`.

**Check yourself:** Q10.

---

## Chapter 8 — Generational GC

### 8.1 The observation
**Most objects die young** (temporary strings, intermediate results). A few live very long. Tracing the *whole* heap every time
wastes effort on old data that is almost always still live.

### 8.2 The design
```
YOUNG:  EDEN (new objects) | SURVIVOR A | SURVIVOR B           OLD
```
* **Minor GC**: collect only the young generation. Survivors are copied (Eden → survivor, survivor → other survivor).
  Each survival increments the object's **age**. At the **promotion threshold** the object is **promoted** (copied to Old).
* **Major (full) GC**: trace and sweep the whole heap; needed rarely, when Old fills.
* Two survivor spaces alternate: one holds survivors, the other is the empty "to" space.

With threshold 2: Eden (age 0) → Survivor A (1) → Survivor B (2) → **Old** on the third minor GC.

### 8.3 The catch
A minor GC does not trace the old generation — but old objects may point at young ones. That is chapter 9.

### 8.4 Experiments
* Example **7. Aging and promotion**: follow `keeper` through the four minor GCs; watch the **age** badge and the region change.
* Example **9. Allocation churn**: no `gc()` call at all; open the *Details* tab and see minor collections happened on their own
  when Eden filled — and `Reclaimed` counts the garbage.

**Where in the code:** `gc_generational.cpp` (`minor`, `major`, `evacuate`). Tests: `generational.*`.

**Check yourself:** Q11, Q12.

---

## Chapter 9 — Write barrier, remembered set, card table

### 9.1 The problem in one picture
```
Old #1 ──► Young #5          (stored after #1 was promoted; no root reaches #5 directly)
```
A minor GC starts from the roots and only traces young objects. It would find no path to #5 and **free an object that is still
in use**.

### 9.2 The fix
A **write barrier** is code that runs on *every reference store*. If the holder is **old** and the new target is **young**, it
records the holder in the **remembered set** (and dirties a **card**). During a minor GC the remembered objects are treated as
**extra roots**.

* **Remembered set**: precise list of old objects that may point young.
* **Card table**: old space divided into cards (128 B); a store marks the holder's card dirty; the GC scans objects in dirty
  cards. Cheaper to update, coarser to scan. (Add `set cards on` to your program to make the GC scan cards instead of the remembered set.)

### 9.3 Experiment — example **8. Write barrier & remembered set**
Step to `old.next = young`: the **Details** tab draws `WRITE Old → Young ⇒ WRITE BARRIER ⇒ REMEMBERED SET`. Then `young = null`
(only the old object references it) and `gc(minor)`: `young` survives. To see why the barrier matters, read the test
`barrier.without_the_barrier_the_young_object_would_be_lost`, which bypasses the barrier and shows the object being freed.

**Where in the code:** `barrier.h`/`barrier.cpp`, `GenerationalCollector::onReferenceStore`, the "REMEMBERED SET SCAN" step of
`minor()`. Tests: `barrier.*`.

**Check yourself:** Q13.

---

## Chapter 10 — Safepoints and stop-the-world

A moving collector cannot run while the program is changing the heap. Threads therefore **poll a flag at safepoints** and park.
HeapLens simulates threads deterministically: thread *i* needs a fixed number of "ticks" to reach its next poll.

State machine: `RUNNING → GC_REQUESTED → SAFEPOINT → STOPPED → GC → RESUMED → RUNNING`.
Every GC in HeapLens is wrapped by this protocol (`Runtime::collect`). Watch it in the **Details → Safepoints** view during any collection;
the number of simulated threads is fixed at 3 in the UI.

**Where in the code:** `safepoint.h`/`safepoint.cpp`. Tests: `safepoint.*`.

**Check yourself:** Q14.

---

## Chapter 11 — The architecture: one stream of events

The central design decision: **collectors never draw anything.** They emit small structured **events** (`ObjectMarked`,
`ObjectMoved`, `ReferenceUpdated`, …), each with a plain-language `text` explaining *what and why*. Logger, statistics, and the UI
all consume the same stream.

```
collector ──events──► EventStream ──► Logger / Metrics / (JSON) ──► browser UI
```
The server answers a command with `{before snapshot, events, after snapshot}`; the browser **replays** the events on a copy of
`before`. This makes the UI a thin layer, makes demos deterministic (same input ⇒ identical events), and makes every behaviour
testable without a UI.

See [architecture.md](architecture.md) and [visualization.md](visualization.md).

---

## Chapter 12 — Code tour (read in this order)

| Step | File | What to look for |
|---|---|---|
| 1 | `include/minigc/object.h` | the 24-byte header; why free blocks reuse it |
| 2 | `include/minigc/heap.h`, `src/core/heap.cpp` | `Memory`, `BumpAllocator`, `FreeListAllocator` (split/coalesce), `Heap::forEachBlock` |
| 3 | `include/minigc/runtime.h`, `src/core/runtime.cpp` | `allocate` (retry after GC), `setRef` (calls the collector hook), `collect` (safepoint bracket) |
| 4 | `include/minigc/collector.h` | the `Collector` interface and the shared `Tracer` |
| 5 | `src/core/gc_marksweep.cpp` | the shortest complete collector |
| 6 | `src/core/gc_markcompact.cpp` | forwarding addresses, why the pass order matters |
| 7 | `src/core/gc_copying.cpp` | `evacuate` + the Cheney scan loop |
| 8 | `src/core/gc_generational.cpp` | `minor`, `major`, promotion rules, remembered-set maintenance |
| 9 | `src/core/gc_refcount.cpp` | the eager, non-tracing outlier |
| 10 | `src/core/events.cpp`, `visualization.cpp` | events → JSON |
| 11 | `src/core/commands.cpp`, `src/server.cpp` | the command language and the tiny HTTP server |
| 12 | `ui/model.js` | `applyEvent`: the replay brain |
| 13 | `ui/script.js`, `ui/playground.js` | the little language and its executor |

Tip: in each collector, read the `emit(...)` calls — they narrate the algorithm.

---

## Chapter 13 — Tests are the specification

Run them (`build\minigc_tests.exe`, or `./build.sh test`): 120 tests. Good ones to read first:

* `marksweep.event_sequence_matches_the_story` — the exact event order of a mark-sweep.
* `compact.events_show_old_and_new_addresses` — forwarding & movement.
* `generational.survivors_age_and_are_promoted` — Eden → A → B → Old.
* `barrier.without_the_barrier_the_young_object_would_be_lost` — why barriers exist.
* `graph.*_agrees_with_reference` — each collector vs the reachability oracle on random graphs.
* `events.same_scenario_gives_identical_event_streams` — determinism.

`consistent()` in `tests/test_util.h` walks every region and checks every reference after a GC — it is the safety net.

---

## Chapter 14 — Comparison cheat-sheet

| | Ref counting | Mark-Sweep | Mark-Compact | Copying | Generational |
|---|---|---|---|---|---|
| Finds garbage by | counts hitting 0 | tracing | tracing | tracing (copy) | tracing young often |
| Handles cycles | **no** | yes | yes | yes | yes |
| Moves objects | no | no | **yes** (slide) | **yes** | **yes** (young) |
| Fragmentation | yes | **yes** | none | none | young none, old yes |
| Pause | none (small, per store) | ∝ heap | ∝ heap | ∝ **live** data | short minors, rare long majors |
| Extra per-store cost | **yes** (counts) | none | none | none | **yes** (write barrier) |
| Extra memory | count field | mark bits | forward field | **2× space** | survivor spaces + remembered set |

---

## Exercises (do them in the playground)

1. **Leak on purpose.** Write a program where a chain of 5 objects forms a ring (`a.next = b` … `e.next = a`), drop all variables, run `gc()` under `rc`, then under `marksweep`. Explain the difference.
2. **Fragmentation.** Under `marksweep`, allocate 8 objects, drop every other variable, `gc()`. Read the free-block count and fragmentation %. Then `use compact` and `gc()` again.
3. **Promotion threshold.** Add `set promotion 0` under `use generational` in example 7, then try `set promotion 5`. How many minor GCs until promotion in each case?
4. **Remembered set vs cards.** Run example 8 with and without `set cards on` under `use generational`. What differs in the *Algorithm state* panel?
5. **Survivor overflow.** Run example 10 (*Tuning: survivor overflow*): 13 live objects, but a survivor space holds only 8 objects of 32 bytes. Where do the other 5 go? (Check the heap rows.)
6. **Prediction game.** Before pressing Step on any example, write down which objects will be reclaimed. Compare.

### Stretch (code changes)
* Make `promotionThreshold` adaptive (promote earlier when survivor space is nearly full).
* Add a "best fit" mode to `FreeListAllocator` and compare fragmentation.
* Add a new demo to `Session::demos()` and a matching example to `ui/examples.js`; add a test.

---

## Answers to "check yourself"

* **Q1** Leaks (never freed) and dangling pointers/double frees (freed too early/twice). GC removes the second and greatly reduces the first.
* **Q2** Garbage = no path from any root. Age and size are irrelevant.
* **Q3** Yes: an object that is garbage still occupies memory until a collection runs.
* **Q4** Bump: allocation = move a cursor (fast, no fragmentation, no individual free). Free list: reuses holes, supports freeing, can fragment.
* **Q5** Free space split into small holes so a large request fails; `1 − largest/total`.
* **Q6** In a cycle every object has an incoming reference from another (unreachable) object, so no count reaches 0.
* **Q7** Roots → gray; scan gray objects turning newly reached ones gray and themselves black; white at the end = garbage.
* **Q8** It does not move live objects, so fragmentation remains; cost ∝ heap because the sweep walks everything.
* **Q9** The referenced object's address changes; a reference stored elsewhere (even in an unmoved object) would point at stale memory.
* **Q10** Survivors are copied contiguously into an empty space and garbage is left behind, so free space is always one block; cost ∝ live data.
* **Q11** Most objects die young, so collecting only the small young generation frequently is cheap and effective.
* **Q12** Each time an object survives a minor GC its age increases; at the threshold it is promoted to Old.
* **Q13** A minor GC doesn't trace Old, so an old→young pointer would hide the young object; the barrier records such stores so the object stays reachable.
* **Q14** Moving or freeing objects while the program runs could corrupt references; safepoints park threads at well-defined places so the world is stopped atomically.
