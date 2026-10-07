# Development guide

## Repository layout

```
include/minigc/   public headers (one per component)
src/core/         the runtime library (libminigc_core)
src/main.cpp      CLI front end            src/server.cpp   tiny HTTP server for the UI
tests/            automated tests (own mini framework, no dependencies)
ui/               browser UI: index.html, style.css, model.js (pure), app.js (DOM)
ui/dev/           UI development helpers: mock backend + tests for model.js
docs/             teaching documents
```

## Reading order for the code

1. `object.h` → `heap.h`/`heap.cpp` (memory, allocators)
2. `runtime.h`/`runtime.cpp` (allocate / setRef / roots / collect)
3. `collector.h` + `gc_common.cpp` (the shared `Tracer`)
4. `gc_marksweep.cpp` → `gc_markcompact.cpp` → `gc_copying.cpp` → `gc_generational.cpp`
5. `gc_refcount.cpp` (the odd one out: eager, no tracing)
6. `events.h`, `visualization.cpp`, `commands.cpp`

## Writing / changing a collector

A collector implements `Collector` (`collector.h`):

* `setup()` — create the regions it needs (called on reset).
* `allocate(size, actual)` — choose a region; return 0 when full (the runtime then triggers a GC).
* `collect(kind)` — do the work **and narrate it with events**.
* optional hooks: `onReferenceStore`, `onRootAdded`, `onRootRemoved`, `attach`.

Rules that keep the project teachable:

1. **Emit events for every logical step**, each with a `text` that says *what happened and why*.
   The test `events.every_event_explains_itself` fails if an event has no explanation.
2. Group work in phases with `rt.beginPhase("NAME", "explanation")`; the UI timeline is built from them.
3. Update references via `updateRootRef` / `updateFieldRef` so `ReferenceUpdated` is emitted.
4. Call `rt.relocate(id, newAddr)` when an object moves and `rt.forget(id)` when it dies.
5. Leave no state behind: after a collection every `forward` field is 0 and colours are white
   (`consistent()` in `tests/test_util.h` checks this).
6. Register it in `makeCollector` (`gc_common.cpp`), add a key in `parseAlgo`, a phase list in
   `ui/model.js` (`PHASES`) and tests.

## Tests

* Framework: `tests/test_framework.h` — `TEST("suite.name") { CHECK(...); CHECK_EQ(a, b); }`.
* Helpers in `tests/test_util.h`: `cfgFor`, `node`, `consistent()`/`CHECK_CONSISTENT`, `buildRandomGraph`.
* **Cross-checks:** every tracing collector is compared against `ObjectGraph::reachable()` on
  randomly generated graphs (`graph.*_agrees_with_reference`).
* **Regression rule:** when a bug is found, add a test named `<suite>.regression_<what>` that fails
  before the fix.
* New suite? Add its prefix to the `foreach(suite ...)` list in `CMakeLists.txt` so `ctest` runs it.

### UI tests (optional, needs Node.js ≥ 14)

`ui/model.js` (the replay logic and layout) is pure JavaScript and has its own tests; they run
against a small mock backend, not against the C++ binary:

```bash
node ui/dev/test-model.js     # replay model + layout
node ui/dev/test-script.js    # playground language compiler + examples
node ui/dev/mock-server.js 8081      # mark-sweep only mock, for working on the UI without a build
```

## Determinism checklist

* Object ids and root numbers are sequential and restart on reset.
* Containers with defined iteration order only (`std::map`/`std::set`, vectors).
* No wall-clock time, no randomness (the "random" test graphs use a fixed LCG seed).
* Simulated threads poll at fixed distances.

## Coding style

Plain C++17, no macros except in tests, comments explain *why*. Prefer clarity over speed — see the
project philosophy in [README.md](README.md).
