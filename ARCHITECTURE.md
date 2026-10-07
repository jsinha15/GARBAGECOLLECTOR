# Architecture

## Components

```
Runtime                                   (include/minigc/runtime.h)
│
├── Heap            raw bytes + Regions, each owned by an Allocator      heap.h
│    └── Allocator  BumpAllocator | FreeListAllocator (split + coalesce)  heap.h
├── ObjectModel     24-byte header + reference slots + payload           object.h
├── ReferenceManager  reference slots hold heap addresses; setRef() goes through the collector hook
├── RootManager     global + (simulated) stack roots                     roots.h
├── ObjectGraph     DFS / BFS / reachability / explanations (ground truth) graph.h
├── GenerationManager  the Eden / SurvivorA / SurvivorB / Old layout      GenerationalCollector::setup
├── BarrierManager  write barrier + remembered set + card table          barrier.h
├── SafepointManager  stop-the-world protocol over simulated threads     safepoint.h
├── GarbageCollector  the `Collector` interface                           collector.h
│    └── GCAlgorithms  RefCount | MarkSweep | MarkCompact | Copying | Generational   gc_algorithms.h
├── Metrics         counters fed by the event stream                      events.h
└── Visualization   snapshot + event → JSON, ASCII views                  visualization.h
```

### Decoupling rules

1. **The runtime only knows the `Collector` interface.** Swapping algorithms is one call
   (`Runtime::setAlgorithm`). The three algorithms that share the single free-list layout
   (reference counting, mark-sweep, mark-compact) can even be swapped on a live heap.
2. **The allocator knows nothing about GC.** It hands out and takes back blocks of one region.
3. **GC algorithms never touch the UI.** They `emit()` events. Everything else subscribes.

## The event stream — the most important design decision

```
              GC ENGINE (collectors, allocator hooks, barrier, safepoints)
                          │  Event{seq,type,id,id2,addr,addr2,value,phase,text}
                          ▼
                    EventStream  ── keeps history, stamps `phase` and `seq`
              ┌───────────┼───────────────┐
              ▼           ▼               ▼
           Logger      Metrics       Visualization ──► JSON ──► browser UI
```

* Events are **plain data**. `text` is the plain-language explanation ("WHY"), written by the
  component that knows the reason.
* `seq` restarts at 1 on every reset; the same script therefore yields a **byte-identical** event
  stream — the basis of deterministic demos and of the regression tests.
* The UI performs *replay*: it gets `before` snapshot + `events` + `after` snapshot and applies the
  events one at a time to a copy of `before`. See [VISUALIZATION.md](VISUALIZATION.md).

### Event catalogue

| Area | Events |
|---|---|
| lifecycle | `GCStarted/Completed`, `MinorGCStarted/Completed`, `MajorGCStarted/Completed`, `PhaseStarted` |
| tracing | `RootDiscovered`, `ObjectDiscovered`, `ObjectMarked`, `ObjectScanned`, `ReferenceTraversed`, `MarkingCompleted`, `ObjectUnreachable` |
| reclaim | `ObjectReclaimed`, `FreeListRebuilt` |
| movement | `ForwardingAssigned`, `ObjectMoved`, `ReferenceUpdated`, `SpacesSwapped` |
| generational | `ObjectAged`, `ObjectPromoted`, `WriteBarrierTriggered`, `RememberedSetAdded/Removed`, `CardDirtied/Cleaned` |
| ref counting | `RefCountChanged`, `CycleDetected` |
| mutator | `ObjectAllocated`, `ReferenceStored`, `RootAdded/Removed`, `AllocationFailed`, `OutOfMemory` |
| safepoints | `SafepointRequested`, `ThreadReachedSafepoint`, `AllThreadsStopped`, `ThreadsResumed` |

The meaning of `id`, `id2`, `addr`, `addr2`, `value` per event is tabulated in
[docs/visualization.md](docs/visualization.md).

## Memory layout

The heap is a real byte buffer (`Memory`) addressed by *simulated* addresses starting at `0x1000`
(so `0` can mean null). Objects are stored in it for real: a 24-byte `ObjectHeader`, `numRefs` 4-byte
address slots, then payload. Moving collectors really `memmove` bytes and really rewrite the address
slots — that is what makes forwarding and reference updating visible and testable.

Every region can be walked linearly, block after block, because free blocks carry a header too
(`id == 0`). Walking a region is how mark-sweep sweeps, how compaction assigns addresses, and how the
tests check heap consistency.

| Algorithm | Regions | Allocation |
|---|---|---|
| RC / Mark-Sweep / Mark-Compact | `HEAP` (free list) | first fit, split, coalesce |
| Copying | `FROM-SPACE`, `TO-SPACE` (bump) | bump pointer in from-space |
| Generational | `EDEN`, `SURVIVOR A/B` (bump), `OLD` (free list) | Eden bump; objects larger than a survivor space (256 B) go straight to Old |

## Stop-the-world

`Runtime::collect()` brackets every collection with the safepoint protocol
(`RUNNING → GC_REQUESTED → SAFEPOINT → STOPPED → GC → RESUMED → RUNNING`) over simulated threads.
Collections are not concurrent, so no mutator code runs while a collector executes.

## Error handling

Operations that can legitimately fail (`allocate`, `setRef`, `addRoot`) return `0`/`false` and the
command layer reports it. A *promotion failure* in the generational collector (old generation full
while evacuating) throws `std::runtime_error` after emitting `OutOfMemory`; the safepoint bracket
resumes the world before the exception propagates.
