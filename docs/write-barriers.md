# Write barriers

Source: `include/minigc/barrier.h`, `src/core/barrier.cpp`, `GenerationalCollector::onReferenceStore`.

## 1. Concept
A **write barrier** is a small piece of code the runtime executes on *every reference store* (`src.slot = dst`). In HeapLens's generational
collector it detects stores that create an **old → young** pointer and records them.

## 2. Why it exists
A minor GC traces only the young generation, starting from the roots. Consider:

```
Old #100 ──► Young #500        (stored AFTER #100 was promoted; no root reaches #500 directly)
```
Without help, minor GC finds no path to #500 and **frees an object that is still in use**. Tracing all of Old to be safe would defeat the
purpose of a minor GC. The barrier turns "who points into the young generation?" from a heap scan into a short list.

## 3. Data structures
`BarrierManager` = `RememberedSet` (set of old object ids) + `CardTable` (one byte per `cardBytes` of Old). The barrier records into both.

## 4. Algorithm
```
onWrite(src, dst):
   if src is in Old and dst is in Young:           // the only interesting case
        emit WriteBarrierTriggered
        mark card(src) dirty        -> CardDirtied (if it was clean)
        remembered.insert(src)      -> RememberedSetAdded (if new)
   else: nothing (young→young, old→old, young→old, null stores need no record)
```

## 5. Example (Demo 5)
```
WRITE        Old #1 ──► Young #5
             ↓ WRITE BARRIER
REMEMBERED SET   { #1 }
```
`tests/test_generational.cpp`: `barrier.without_the_barrier_the_young_object_would_be_lost` writes the field *behind the barrier's
back* and shows the young object being reclaimed — the failure the barrier prevents.

## 6. Visualization
The Algorithm panel draws exactly the diagram above when `WriteBarrierTriggered` fires, with the *Why* sentence; the remembered-set chips
and dirty cards update on the following events.

## 7. Tests
`barrier.old_to_young_store_is_recorded`, `barrier.young_to_young_and_old_to_old_stores_are_ignored`,
`barrier.null_stores_and_young_to_old_stores_are_ignored`, `barrier.only_the_generational_collector_uses_barriers`,
`barrier.without_the_barrier_the_young_object_would_be_lost`.

## 8. How to run it
`demo 5`, or: `demo 4`, press `gc` 3×, `new Node 2 0 Y`, `ref A 0 Y` (A is old now) and read the events (`events 5`).
