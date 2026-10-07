# Mark-Sweep

Source: `src/core/gc_marksweep.cpp`, shared tracer in `src/core/gc_common.cpp`.

## 1. Concept
Two phases. **Mark:** starting at the roots, find everything reachable and mark it. **Sweep:** walk the *whole* heap; every block whose
object is not marked is garbage and goes back to the free list. Live objects never move.

## 2. Why it exists
It reclaims cycles (reference counting cannot) and costs nothing on pointer stores. Its costs: a pause proportional to the heap, and
fragmentation (live objects stay scattered).

## 3. Data structures
* Tri-colour mark state in the header: **white** (not reached) → **gray** (discovered, in the worklist) → **black** (scanned).
* A FIFO worklist of gray objects (`Tracer`, `collector.h`) — FIFO gives the breadth-first order of the diagrams.
* The region's `FreeListAllocator`.

## 4. Algorithm
```
GC start: world is stopped
ROOT SCAN : for each root r: emit RootDiscovered; if target is white: make it gray   (ObjectDiscovered + ObjectMarked)
MARK      : while worklist not empty:
              x = pop; emit ObjectScanned(x); x.color = black
              for each reference x→y:  emit ReferenceTraversed(x,y);  if y is white: make y gray
            emit MarkingCompleted; every still-white object: emit ObjectUnreachable("no path exists from any GC root")
SWEEP     : walk blocks in address order; white → ObjectReclaimed + becomes a free block; black → reset to white
            rebuild the free list (adjacent free blocks coalesce); emit FreeListRebuilt, GCCompleted
```

## 5. Example (the worked story from the project brief)
```
1 BEFORE      Root→A→B→C   D→E
2 ROOT SCAN   "A is reachable because it is directly referenced by GC root 'rootA'"   A: UNVISITED → DISCOVERED
3 MARK A      A: SCANNED;  A→B traversed;  B: DISCOVERED
4 MARK B      B: SCANNED;  B→C;            C: DISCOVERED
5 COMPLETE    A, B, C: LIVE        D, E: UNREACHABLE ("No path exists from any GC root")
6 SWEEP       D: RECLAIMED, E: RECLAIMED
7 FINAL       [A][B][C][FREE…]
```
Neither D nor E was removed for being *old*; they were removed because **no GC root can reach them**.

## 6. Visualization
Timeline `GC START → ROOT SCAN → MARK → SWEEP → FREE LIST`; node states `UNVISITED / DISCOVERED / SCANNED / LIVE / UNREACHABLE /
RECLAIMED` (each with text, not just colour); the pulsing edge shows the reference being followed.

## 7. Tests
`marksweep.paper_example`, `marksweep.live_objects_do_not_move`, `marksweep.event_sequence_matches_the_story`,
`marksweep.breadth_first_discovery_order`, `marksweep.unreachable_events_carry_the_reason`,
`marksweep.colors_reset_after_cycle`, `marksweep.cycles_and_self_references_are_collected`,
`marksweep.free_list_is_coalesced_after_sweep`, `marksweep.statistics`,
`marksweep.age_is_irrelevant_only_reachability_matters`, `graph.marksweep_agrees_with_reference`.

## 8. How to run it
```
./build/minigc --demo 1 --trace        # CLI with the event narration
./build/minigc --serve                 # UI: example 1 → Step … Step
```
