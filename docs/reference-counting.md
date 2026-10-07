# Reference counting

Source: `src/core/gc_refcount.cpp`.

## 1. Concept
Every object stores **how many references point at it** (from heap fields and from roots). Storing a reference increments the
target's count; overwriting or removing one decrements it. When a count reaches **0** nobody can reach the object, so it is freed
immediately — and freeing it removes *its* outgoing references, which may free its children, recursively.

## 2. Why it exists
It is the simplest collector idea and reclaims garbage *promptly and incrementally* (no pause). Its famous flaw is the reason tracing
collectors exist.

## 3. Data structures
`ObjectHeader::refCount`; hooks `Collector::onReferenceStore / onRootAdded / onRootRemoved` called by the runtime on every store.
Memory comes from the same free-list allocator as Mark-Sweep.

## 4. Algorithm
```
store(src.slot = new):  increment(new); decrement(old)         // increment FIRST: storing the same target must not hit 0
increment(o): o.count++
decrement(o): o.count--; if o.count == 0: reclaim(o)
reclaim(o):   copy o's children; free o's block; for each child: decrement(child)    // recursive reclamation
```
`collect()` (the "GC" button) does **not trace**. It (a) frees objects whose count is 0 because nothing ever referenced them,
and (b) *diagnoses* leaks: it asks the question a tracing collector would ask — "is any still-counted object unreachable?" — and reports
`CycleDetected`.

### The cycle problem
```
A ─────► B          root ► A
▲        │          A.refCount = 2 (root + B)   B.refCount = 1 (A)
└────────┘          remove the root:  A = 1, B = 1   ← neither reaches 0
```
Each object is kept alive by *the other, unreachable* object. **Reference counting cannot reclaim this cycle because each object still
has an incoming reference from another unreachable object.**

## 5. Example
`demo 2` builds exactly this graph; the UI shows `A refCount = 1`, `B refCount = 1`, an **UNREACHABLE CYCLE** alert, and the heap
still holding both objects. `demo 2 marksweep` + `gc` reclaims them.

## 6. Visualization
Each block/node shows `rcN`; `RefCountChanged` events animate the number; the Algorithm panel lists all counts and raises the cycle
banner (from `ObjectGraph::unreachableCycles`).

## 7. Tests
`refcount.counts_follow_stores_and_roots`, `refcount.zero_count_reclaims_immediately_and_recursively`,
`refcount.shared_object_survives_one_owner_leaving`, `refcount.overwriting_with_same_target_is_safe`,
`refcount.cycle_is_never_reclaimed`, `refcount.tracing_gc_reclaims_the_same_cycle`,
`refcount.switching_from_tracing_gc_recomputes_counts`, `session.demo2_cycle_defeats_reference_counting_but_not_tracing`.

## 8. How to run it
```
./build/minigc --demo 2 --trace
minigc> demo 2           # RC cycle      → gc → still there
minigc> demo 2 marksweep # same graph    → gc → reclaimed
```
