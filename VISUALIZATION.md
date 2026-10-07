# Visualization

The UI is a **thin layer over the runtime's event stream**. It contains no GC logic. This document
explains what the learner sees; the event/field reference is in [docs/visualization.md](docs/visualization.md).

## The code playground

The left column has a **Code** tab: write a small program, press **Step** / **Play**, and the right side shows the variables, objects,
heap, graph and every GC in slow motion (see [docs/playground.md](docs/playground.md)). Everything is driven by code; the
run buttons are Step, Play, Skip to end, Pause and Restart.

## The layout

```
┌ header: collector · demo · heap size · connection ────────────────────────────────────────┐
│ Controls │  HEAP VIEW (regions → blocks)            │ WHAT IS HAPPENING, AND WHY           │
│          │  OBJECT GRAPH (roots, edges, states)     │ OBJECT INSPECTOR                      │
│          │  GC TIMELINE + SAFEPOINT STRIP           │ ALGORITHM STATE (RC counts / barrier…)│
│          │                                          │ STATISTICS                            │
├──────────┴──────────────────────────────────────────┴───────────────────────────────────────┤
│ EVENT STREAM (current event highlighted)         │ GLOSSARY                                 │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

1. **Heap view** — one row per region (`EDEN`, `SURVIVOR A`, `OLD`, `FROM-SPACE`, …) with allocator kind, used bytes,
   free blocks and fragmentation. Blocks are drawn proportionally: objects (label, state, age or refcount), `FREE` gaps
   (hatched) and `RECLAIMED` blocks (struck through). Click a block to inspect it.
2. **Object graph** — roots on top, reachable objects by BFS depth, and a separate band for objects **not reachable from any
   root** (so “reachable vs unreachable” is visible before any GC runs). The edge/object currently being processed pulses.
3. **GC timeline** — the phases of the selected collector (`ROOT SCAN → MARK → SWEEP → …`) with the current one highlighted,
   and underneath the **safepoint strip** (`RUNNING → GC REQUESTED → SAFEPOINT → STOPPED → GC → RESUMED`) with one lane per
   simulated application thread.
4. **Explanation panel** — for each step: **EVENT**, **WHY** (the event's text) and **WHAT CHANGED** (state transitions,
   address changes, count changes). For moves it also shows *old reference → new reference*.
5. **Inspector** — id, type, address (and forwarding address while moving), size, generation/space, age, reference count,
   GC state, outgoing references, incoming references (including roots) and the **reachability reason**:
   `Root 'rA' -> A (#1) -> B (#2)` or `No path exists from any GC root to D (#4).`
6. **Algorithm panel** — reference counts and the *unreachable cycle* alert (RC); write barrier flow, remembered set and card
   table (generational); short “how it works” text for the others.
7. **Controls** — **Step**, **Play**, **Skip to end**, **Pause**, **Restart** for the program, and **Next / Auto / Skip / Replay**
   for a garbage collection in slow motion. Objects, references, roots, GCs and tuning are all written as code.

## States are never colour-only

| State | Meaning | Also encoded as |
|---|---|---|
| `REACHABLE` / `LIVE` | a path from a root exists / marking finished | green fill, solid border |
| `UNVISITED` | white: not reached yet | white fill |
| `DISCOVERED` | gray: found, waiting in the worklist | yellow, **dashed** border |
| `SCANNED` | black: references followed | dark fill |
| `UNREACHABLE` | no path from any root | red tint, **dotted** border |
| `RECLAIMED` | memory returned | hatched, struck-through, dashed |
| `WILL MOVE` / `STAYS` / `MOVED` | compaction / copying | blue |
| `SURVIVED` / `PROMOTED` | generational | purple |

Every block and node prints its state as text (blocks use a 4–5 letter abbreviation, tooltips and the inspector give the full word).

## Replay: how stepping works

A collection runs to completion inside the runtime (stop-the-world). The HTTP response contains:

```
{ ok, message, replay, before: <snapshot>, events: [...], after: <snapshot> }
```

The UI copies `before` and applies **one event per click**. `ObjectDiscovered` + `ObjectMarked` are merged into one step. At the
end it swaps in `after`, so what you finally see is exactly the real heap. Because events are deterministic, **replay** shows the
same sequence every time, and the same scenario always gives the same picture.

All of the replay logic is the pure module `ui/model.js` (`applyEvent`, `heapRows`, `layoutGraph`).

## Event → visual mapping

| Event | Visual effect |
|---|---|
| `GCStarted` / `MinorGCStarted` | all objects become `UNVISITED` (old objects `NOT TRACED` in a minor GC) |
| `RootDiscovered` | the root box and its target pulse |
| `ObjectDiscovered/Marked` | object → `DISCOVERED` |
| `ObjectScanned` | object → `SCANNED` |
| `ReferenceTraversed` | the edge pulses |
| `MarkingCompleted` | marked objects → `LIVE` |
| `ObjectUnreachable` | object → `UNREACHABLE` |
| `ObjectReclaimed` | block becomes hatched `RECLAIMED` |
| `ForwardingAssigned` | forwarding address shown on the block; `WILL MOVE` / `STAYS` |
| `ObjectMoved` | the block jumps to its new address (possibly into another region) |
| `ReferenceUpdated` | edge pulses; old → new reference shown; root/field address rewritten |
| `ObjectAged` / `ObjectPromoted` | age badge changes; purple state |
| `WriteBarrierTriggered` / `RememberedSetAdded` / `CardDirtied` | barrier flow diagram, remembered-set chip, dirty card |
| `RefCountChanged` | `rc` badge changes |
| `CycleDetected` | cycle members → `UNREACHABLE CYCLE` plus an explanation banner |
| safepoint events | the strip advances; thread lanes turn red when parked |

## Keyboard

`N` or `→` executes one step while a replay is active.
