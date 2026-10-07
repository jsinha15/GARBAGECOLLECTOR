# Mark-Compact

Source: `src/core/gc_markcompact.cpp`.

## 1. Concept
Like Mark-Sweep, but instead of leaving holes it **slides live objects together** so all free space becomes one block. This is the
"sliding" (Lisp-2) variant: objects keep their relative order.

## 2. Why it exists
Fragmentation: after a sweep you can have plenty of free bytes yet no single block big enough for a new object. Compaction fixes that
at the price of moving objects — and moving objects means **every reference to them must be rewritten**.

## 3. Data structures
The header's `forward` field holds an object's *new* address (the **forwarding information**) between phases 2 and 4.

## 4. Algorithm (four passes)
```
1 MARK                  same tracer as Mark-Sweep
  RECLAIM               dead objects are discarded (space will be overwritten)
2 COMPUTE ADDRESSES     dest = start of heap
                        for each live object in address order: obj.forward = dest; dest += obj.size
3 UPDATE REFERENCES     for each root:                 root = target.forward
                        for each reference field of each live object: field = target.forward
                        (objects have NOT moved yet, so the old headers and their forward fields are still readable)
4 MOVE OBJECTS          for each live object in address order: memmove(obj.forward, obj, size); update table; forward = 0
FREE SPACE              one free block from dest to end of heap
```
Order matters: addresses first, references second, moves last. Moving lowest-address-first is safe because an object only ever moves
*down*, never onto a not-yet-moved object.

## 5. Example
```
BEFORE   [A][FREE][B][FREE][C][FREE…]          A: stays  B: 0x1040 → 0x1020  C: 0x1080 → 0x1040
Reference  A.refs[0]   old reference → 0x1040   new reference → 0x1020          (A did not move, but its pointer must change!)
AFTER    [A][B][C][FREE………]
```
That last line is the key lesson: **a reference to an object that moved becomes wrong even if the holder did not move.**

## 6. Visualization
Timeline `COMPUTE ADDRESSES → UPDATE REFERENCES → MOVE OBJECTS`; each block shows `WILL MOVE` / `STAYS` and the forwarding address;
in the explanation panel *old reference → new reference* is spelled out for each rewrite; during the move step blocks visibly jump.

## 7. Tests
`compact.slides_live_objects_together`, `compact.free_space_becomes_one_block`, `compact.references_and_roots_are_updated`,
`compact.events_show_old_and_new_addresses`, `compact.payload_survives_relocation`, `compact.already_compact_heap_is_untouched`,
`compact.preserves_relative_order`, `compact.after_marksweep_fragmentation_is_removed`, `compact.cycles_survive_and_are_updated`,
`graph.markcompact_agrees_with_reference`, `session.demo3_compaction`.

## 8. How to run it
```
./build/minigc --demo 3 --trace        # (mark-sweep makes the holes, mark-compact is then selected)
minigc> gc
```
