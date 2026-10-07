# HeapLens documentation

Each page follows the same eight-part template — **Concept → Why it exists → Data structures → Algorithm →
Example → Visualization → Tests → How to run it** — and is meant to be read next to the matching source file.

| # | Page | Source | Question it answers |
|---|---|---|---|
| ★ | [learning-guide.md](learning-guide.md) | whole repo | Learn everything from scratch, chapter by chapter, with exercises. |
| ★ | [deployment.md](deployment.md) | `Dockerfile` | How do I put it online for free? |
| ★ | [presenting.md](presenting.md) | — | How do I demo it? Scripts, checklists, Q&A. |
| 0 | [playground.md](playground.md) | `ui/` | How do I write code and watch memory + the GC react? |
| 1 | [getting-started.md](getting-started.md) | — | How do I build it, run it, and try the five demos? |
| 2 | [architecture.md](architecture.md) | `runtime.h` | How do the pieces and the event stream fit together? |
| 3 | [memory-model.md](memory-model.md) | `heap.h` | What *is* the heap? How are blocks allocated, split, coalesced? |
| 4 | [object-model.md](object-model.md) | `object.h` | What does a managed object look like in memory? |
| 5 | [roots-and-reachability.md](roots-and-reachability.md) | `roots.h`, `graph.h` | What is a root? What does "garbage" mean? |
| 6 | [reference-counting.md](reference-counting.md) | `gc_refcount.cpp` | Why can't counting reclaim a cycle? |
| 7 | [mark-sweep.md](mark-sweep.md) | `gc_marksweep.cpp` | How does tracing find garbage, and how is it swept? |
| 8 | [mark-compact.md](mark-compact.md) | `gc_markcompact.cpp` | How do objects move, and why must references be updated? |
| 9 | [copying-gc.md](copying-gc.md) | `gc_copying.cpp` | Why does copying remove fragmentation? |
| 10 | [generational-gc.md](generational-gc.md) | `gc_generational.cpp` | Why generations? What are minor GC and promotion? |
| 11 | [write-barriers.md](write-barriers.md) | `barrier.h` | Why does every pointer store run extra code? |
| 12 | [remembered-sets.md](remembered-sets.md) | `barrier.h` | What does the barrier remember, and how is it used? |
| 13 | [safepoints.md](safepoints.md) | `safepoint.h` | How does the world get stopped? |
| 14 | [visualization.md](visualization.md) | `visualization.cpp`, `ui/` | What does each event look like on screen? |
| 15 | [troubleshooting.md](troubleshooting.md) | — | It does not build / connect / behave as expected. |

Suggested path for a learner: 1 → 3 → 4 → 5 → 7 → 8 → 9 → 6 → 10 → 11 → 12 → 13.
