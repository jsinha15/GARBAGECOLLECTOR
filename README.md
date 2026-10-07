# HeapLens — a learning laboratory for garbage collection

> *"Let's open the heap and watch the garbage collector think."*

HeapLens is a miniature **managed runtime** written from scratch in modern C++17, built to
teach how garbage collectors actually work. It is **not** a JVM/CLR/Go clone: it is a
small, deterministic, inspectable lab where every mechanism is visible — in the code,
in the event stream, and in a browser UI that lets you **step through a collection one
logical step at a time**.

| Collector | What you can watch |
|---|---|
| Reference Counting | counts changing on every store, recursive freeing, and the **cycle it can never reclaim** |
| Mark-Sweep | roots → white/gray/black marking → sweep → free list, with the *reason* each object dies |
| Mark-Compact | forwarding addresses, old → new address, every reference being rewritten, objects sliding together |
| Copying (Cheney) | from-space → to-space, forwarding pointers, space swap, why fragmentation disappears |
| Generational | Eden → Survivor A → Survivor B → Old, ages, promotion, minor vs major GC |
| + infrastructure | write barrier, remembered set, card table, safepoints / stop-the-world |

## Quick start (everything happens in the browser)

You need a **C++17 compiler** — CMake is optional (see [BUILD.md](BUILD.md)). No other libraries, no npm, no internet.

```bash
./build.sh                    # compiler only (Windows/Visual Studio: build_msvc.bat; or use CMake)
./build/minigc --serve        # start the backend + UI
```

Open **http://127.0.0.1:8080/**. On the left, pick an example in the **Code** tab (or write your own program) and press
**Step**; on the right watch your variables, the heap, the object graph and each garbage collection in slow motion,
with plain-English explanations of *what* happens and *why*. You never need to type commands in a terminal.

* **Windows: double-click `start.bat`** to build (first run), start the server and open the UI in one go.
* New to garbage collection? Follow the **[learning guide](docs/learning-guide.md)**. Going to demo it? Read **[presenting.md](docs/presenting.md)**.
* The **code playground** is described in [docs/playground.md](docs/playground.md).
* Everything is driven by code: variables, objects, references, collections and tuning (`set promotion 1`, `set cards on`, `set heap 4096`) are all statements in the editor. 11 ready-made examples are in the dropdown.
* Want to put it online for free? See **[DEPLOY-RENDER.md](DEPLOY-RENDER.md)** (chosen plan) and **[docs/deployment.md](docs/deployment.md)** (alternatives).

## How it is organised

```
Runtime ── Heap ── Allocator (bump / free-list)      GC engine ──events──► Event stream
   │                                                                          │
   ├─ Object model + roots + object graph                            ┌────────┴────────┐
   ├─ Collector interface ◄── RC | Mark-Sweep | Mark-Compact |      Logger/Stats   Visualization
   │                            Copying | Generational                                │
   ├─ BarrierManager (write barrier, remembered set, cards)                          UI
   └─ SafepointManager (stop-the-world)
```

The key design rule: **GC algorithms never touch the UI.** They emit structured events
(`ObjectMarked`, `ObjectMoved`, `ReferenceUpdated`, `ObjectPromoted`, …). The logger, the
statistics and the visualization are just subscribers. See [ARCHITECTURE.md](ARCHITECTURE.md).

## Documentation map

| Document | Purpose |
|---|---|
| [BUILD.md](BUILD.md) | prerequisites, build, test, run — exact commands |
| [ARCHITECTURE.md](ARCHITECTURE.md) | components, data flow, the event stream |
| [DEVELOPMENT.md](DEVELOPMENT.md) | code layout, how to add a collector, testing rules |
| [VISUALIZATION.md](VISUALIZATION.md) | the UI, the event→visual mapping, the replay model |
| [docs/](docs/README.md) | one teaching document per concept (memory model → generational GC → safepoints) |

## Scope

Learning first: understandability, correctness, visualization, inspectability. Explicitly
**out of scope**: concurrent/parallel GC, NUMA, JIT, weak references/finalizers, adaptive
tuning, research-grade algorithms. Everything in scope is implemented in simple, readable code.
