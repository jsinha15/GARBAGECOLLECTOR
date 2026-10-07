# Architecture (teaching version)

The authoritative reference is [../ARCHITECTURE.md](../ARCHITECTURE.md); this page gives the *reasoning*.

## 1. Concept
A managed runtime = **mutator** (the user program that allocates and stores references) + **collector** (reclaims memory the
mutator can no longer reach). HeapLens separates them with narrow interfaces so that you can swap the collector underneath the
same program and compare.

## 2. Why it exists
If the heap, the collector and the UI were entangled you could not tell *which part* causes a behaviour. Three seams keep
things honest:

* `Allocator` — hands out blocks; knows nothing about liveness.
* `Collector` — decides what is live; knows nothing about drawing.
* `EventStream` — the one-way pipe from collector to everything else.

## 3. Data structures
* `Runtime` owns `Heap`, `RootManager`, `BarrierManager`, `SafepointManager`, `EventStream`, `Metrics` and one `Collector`.
* `Event` — `{seq, type, id, id2, addr, addr2, value, phase, text}`.
* `Session` (`commands.h`) — wraps a `Runtime` with a text command language; the CLI, HTTP server and tests all use it.

## 4. Algorithm — life of `gc`
```
Session::run("gc")
  Runtime::collect()
     SafepointManager::requestStop()   ->  events: SafepointRequested, ThreadReachedSafepoint*, AllThreadsStopped
     Collector::collect()              ->  events: GCStarted, PhaseStarted, ... , GCCompleted
     SafepointManager::resume()        ->  event : ThreadsResumed
  Session returns {before, events, after}
```

## 5. Example
Everything the collector does is observable from outside: run `./build/minigc --demo 1 --trace` and read the events
in order — this *is* the collector's thought process.

## 6. Visualization
The UI subscribes to nothing at runtime; it receives the event list per command and replays it. See
[visualization.md](visualization.md).

## 7. Tests
`events.*` (determinism, every event explains itself, sequence numbers), `session.*` (commands, demos).

## 8. How to run it
`./build/minigc --demo 1 --trace`, or `events 50` at the prompt to print the last 50 events.
