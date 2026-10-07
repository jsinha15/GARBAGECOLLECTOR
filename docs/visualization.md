# Visualization: events, snapshots and the UI

Overview of the screen: [../VISUALIZATION.md](../VISUALIZATION.md). This page is the reference for the data contract.

## 1. Concept
`Runtime → events → Visualization model → UI`. The runtime emits structured events; `visualization.cpp` serialises snapshots and events to
JSON; `ui/model.js` replays events; `ui/app.js` draws.

## 2. Why it exists
Because the collector is not allowed to know about drawing, any front end (this web page, a CLI, a test) can consume the same stream.

## 3. Data structures

### HTTP API (served by `minigc --serve`)
| Request | Response |
|---|---|
| `GET /` , `/app.js`, … | static UI files |
| `GET /api/state` | snapshot |
| `GET /api/demos` | `[{number,title,algo,description,next}]` |
| `GET /api/cmd?c=<command>` | `{ok, command, message, replay, before, events, after}` |

### Event fields
| Event | `id` | `id2` | `addr` → `addr2` | `value` |
|---|---|---|---|---|
| `RootDiscovered` | target object | root # | target address | — |
| `ObjectDiscovered/Marked/Scanned/Unreachable` | object | — | its address | `Scanned`: #ref slots |
| `ReferenceTraversed` | holder | target | holder → target address | slot |
| `ObjectReclaimed` | object | — | address | size in bytes |
| `ForwardingAssigned` | object | — | old → **new** address | size |
| `ObjectMoved` | object | — | old → new address | size |
| `ReferenceUpdated` | holder (**0 = a root**) | target | old → new reference | slot, or root # |
| `ObjectAged` / `ObjectPromoted` | object | — | old → new address | new age |
| `WriteBarrierTriggered` | old holder | young target | holder address | — |
| `RememberedSetAdded/Removed` | holder | — | holder address | — |
| `CardDirtied` | holder | — | holder address | card index |
| `RefCountChanged` | object | — | address | **new** count |
| `CycleDetected` | first cycle member | — | — | cycle size |
| `ThreadReachedSafepoint` | **thread index** | — | — | tick |
| `SpacesSwapped` | — | — | new from/survivor start → to start | — |

`phase` is the collector phase active when the event fired; `text` is the explanation.

### Snapshot
`algo, config, regions[], freeBlocks[], objects[] {id,label,type,addr,size,region,age,color,refCount,refs[],live,why}, roots[],
remembered[], cards{}, safepoint{state,threads[]}, cycles[], stats{}`.

## 4. Algorithm — replay
```
state = copy(before)
for each event: applyEvent(state, event) → list of "what changed"; redraw
at the end: state = copy(after)
```
Merges: `ObjectDiscovered`+`ObjectMarked` (same object) are one visible step.

## 5. Example
Mark-sweep of Demo 1 → 25 events (safepoint ×5, `GCStarted`, phases, 3 discoveries, 3 scans, traversals, `MarkingCompleted`,
2 `ObjectUnreachable`, 2 `ObjectReclaimed`, `FreeListRebuilt`, `GCCompleted`, `ThreadsResumed`).

## 6. Visualization
See the mapping table in [../VISUALIZATION.md](../VISUALIZATION.md).

## 7. Tests
`events.*` (determinism, well-formed JSON for every algorithm, explanations, escaping, ASCII views) and, for the JavaScript side,
`node ui/dev/test-model.js`.

## 8. How to run it
`./build/minigc --serve`; or inspect raw JSON: open `http://127.0.0.1:8080/api/cmd?c=gc` in a browser.
