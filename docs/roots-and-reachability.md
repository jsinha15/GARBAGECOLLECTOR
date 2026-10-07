# Roots and reachability

Source: `include/minigc/roots.h`, `include/minigc/graph.h`, `src/core/graph.cpp`.

## 1. Concept
A **GC root** is a reference that lives *outside* the heap: a global variable or a slot on a thread's stack. An object is
**reachable** (live) if a chain of references leads to it from some root. Everything else is **garbage**. This is the only
definition of garbage the tracing collectors use — *not* age, size or "not used for a while".

```
Root
 |
 +----> A ----> B ----> C        reachable: A, B, C
D ----> E                        unreachable: D, E   (nothing points at D)
```

## 2. Why it exists
The collector cannot know what the program "still needs"; it can only observe that no path exists from anything the program can
use. The roots are exactly "what the program can use directly".

## 3. Data structures
* `Root { id, name, kind (Global | Stack), thread, addr }` — `addr` is rewritten when the target moves.
* `RootManager` — `add`, `remove`, `find`; root numbers are sequential and never reused.
* `ObjectGraph` — adjacency built from the heap; `dfs`, `bfs`, `reachable`, `unreachable`, `pathTo`, `explain`,
  `unreachableCycles` (Tarjan SCC).

## 4. Algorithm — reachability analysis (the ground truth)
```
reachable = ∅; queue = targets of all roots
while queue: take x; for each reference x→y: if y ∉ reachable: add, enqueue
```
`explain(id)` runs the same BFS remembering parents, so the answer is a **shortest path**:
`Root 'rA' -> A (#1) -> B (#2) -> C (#3)`, or `No path exists from any GC root to D (#4).`

DFS visits slot 0's subtree first; BFS goes level by level — the tests show the difference (`graph.dfs_and_bfs_orders_differ`).

## 5. Example
```
minigc> demo 1
minigc> why B
B (#2): LIVE. Root 'rootA' -> A (#1) -> B (#2)
minigc> why D
D (#4): UNREACHABLE. No path exists from any GC root to D (#4).
```

## 6. Visualization
Before any GC runs, the graph view separates objects **reachable from a root** (top band, green, `REACHABLE`) from those that are not
(bottom band, red dotted, `UNREACHABLE`). The inspector prints the path or the absence of one.

## 7. Tests
`graph.reachability`, `graph.explains_live_objects_with_a_path`, `graph.explains_unreachable_objects`,
`graph.root_management`, `graph.self_loop_and_cycle_detection`, and the cross-checks `graph.*_agrees_with_reference`
which compare every collector's survivors with `ObjectGraph::reachable()` on random graphs.

## 8. How to run it
`root add r A`, `root add r2 B 1` (a *stack* root of simulated thread 1), `root rm r`, `graph`, `why <obj>`.
