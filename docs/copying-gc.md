# Copying GC (Cheney semispaces)

Source: `src/core/gc_copying.cpp`.

## 1. Concept
Split the heap in two equal **semispaces**. The program allocates in the **from-space** with a bump pointer. To collect, **copy**
every reachable object into the empty **to-space**, then swap the roles. Garbage is never visited: it is simply left behind in the
space that is thrown away.

## 2. Why it exists
* Cost is proportional to the **live** data, not the heap.
* Survivors end up packed side by side, so **fragmentation cannot occur** and allocation is just a pointer bump.
* The price: only half the heap is usable at a time, and objects move (so references must be updated).

## 3. Data structures
* Two bump-allocated regions `FROM-SPACE` and `TO-SPACE` (their names/kinds swap after each GC).
* **Forwarding pointer:** after copying, the old copy's header `forward` field holds the new address, so a second path to the same
  object reuses the copy instead of duplicating it (this also makes cycles and sharing work).

## 4. Algorithm (Cheney's breadth-first scan)
```
evacuate(a): if a.forward: return it                  // already copied
             n = bump-allocate in to-space; copy bytes; a.forward = n; return n
ROOT EVACUATION:  for each root: root = evacuate(root)
SCAN TO-SPACE:    scan = start of to-space
                  while scan < allocation pointer:    // the scan pointer chases the allocation pointer
                      for each reference field of object at scan: field = evacuate(field)
                      scan += object size
DISCARD:          every from-space object without a forward pointer was never reached → ObjectReclaimed
                  clear from-space; swap spaces
```
The to-space itself is the work queue — no extra worklist memory.

## 5. Example
```
FROM  [A][x][B][y][C]        live: A B C           (A→B→C)
COPY  A ──► TO[A]   B ──► TO[A][B]   C ──► TO[A][B][C]      x, y never touched
SWAP  FROM(new) = [A][B][C][ free … ]     TO(new) = empty, ready for the next GC
```

## 6. Visualization
Two rows, `FROM-SPACE` and `TO-SPACE`; each `ObjectMoved` makes a block appear in to-space at the next address; `ReferenceUpdated`
steps show each pointer being redirected; the final swap renames the rows. The fragmentation figure is 0 % afterwards — compare with
Mark-Sweep on the same graph (`demo 6` vs `demo 1`).

## 7. Tests
`copying.layout_is_two_semispaces`, `copying.live_objects_end_up_contiguous_in_the_other_space`,
`copying.no_fragmentation_by_construction`, `copying.forwarding_pointers_and_updated_references`, `copying.spaces_alternate`,
`copying.shared_and_cyclic_objects_are_copied_once`, `copying.allocation_continues_after_the_survivors`,
`copying.auto_gc_when_the_space_fills`, `copying.payload_survives_the_copy`, `graph.copying_agrees_with_reference`,
`session.demo6_copying`.

## 8. How to run it
```
./build/minigc --demo 6 --trace
```
