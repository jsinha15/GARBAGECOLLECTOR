# Getting started

## Prerequisites

* **OS:** Windows 10/11, Linux or macOS.
* **C++ compiler with C++17:** GCC ≥ 9, Clang ≥ 10 or MSVC 2019+.
* **CMake ≥ 3.16.**
* **Frontend tools:** just a web browser. The UI is plain HTML/CSS/JS served by the runtime itself — *no npm, no bundler*.
* **Package managers:** none required.

## Build

```bash
git clone <repository-url> HeapLens
cd HeapLens
cmake -S . -B build
cmake --build build
```

(Windows with Visual Studio: add `--config Debug` to the build command; executables are in `build\Debug\`.
More detail and a no-CMake option: [../BUILD.md](../BUILD.md).)

## Run the visualization (the main way to use HeapLens)

```bash
./build/minigc --serve          # Windows: build\minigc.exe --serve   (add --port 9000 to change the port)
```

Open **http://127.0.0.1:8080/** — the top-right pill must say **connected**. Then:

1. In the **Code** tab choose *1. Basic garbage* from the example list.
2. Press **Step** repeatedly. Each press runs one line; when the program calls `gc()` the collector runs in slow
   motion and **Step** advances it one logical step (or press **N**).
3. Read the orange *What is happening, and why* panel and the *Variables & objects* table; click any object to inspect it.

Everything — creating objects, references, roots, switching collectors, the demos below — is available in the
browser, driven by the small program you type in the editor. See [playground.md](playground.md).

## Run the tests

```bash
ctest --test-dir build --output-on-failure      # CMake build
./build/minigc_tests                            # compiler-only build; add a prefix like  generational.  to filter
```

## Optional: the command line

Handy for scripting; nothing requires it.

```bash
./build/minigc                    # prompt; type `help`
./build/minigc --demo 1 --trace   # run a demo and print each GC event with its explanation
```

Commands: `algo rc|marksweep|compact|copying|generational`, `new Node 2 0 A`, `ref A 0 B`, `root add r A`,
`gc [minor|major]`, `demo 1..6`, `heap`, `inspect A`, `why A`. Switching between `rc`, `marksweep` and `compact`
keeps the heap; switching to or from `copying`/`generational` resets it.

## The five demonstrations

Each one is an example in the editor's dropdown (examples 1, 2, 5, 7, 8 and 6); the `demo N` commands below belong to the optional command line.

### Demo 1 — Basic garbage (`demo 1`, Mark-Sweep)
`Root → A → B` and `C → D`; C's root has been removed. Run **Step GC**: roots discovered → A, B marked and scanned → `C` and
`D` flagged *UNREACHABLE: no path exists from any GC root* → swept. **Expected:** C and D reclaimed; A and B untouched.
The explanation states the reason: they were removed because they were *unreachable*, not because they were old.

### Demo 2 — Cycle (`demo 2`, Reference Counting)
`A → B`, `B → A`, external root removed. Both reference counts stay at 1. Run GC: the cycle is *diagnosed* but not reclaimed.
Then run `demo 2 marksweep` and `gc`: the tracing collector reclaims both.

### Demo 3 — Compaction (`demo 3`, Mark-Compact)
`A, x, B, y, C` were allocated side by side; `x` and `y` died and were swept, leaving FREE holes. Run **Step GC**:
forwarding addresses are assigned, references rewritten, B and C slide down. **Expected:** `[A][B][C][FREE…]`, with old and new
addresses shown.

### Demo 4 — Generational promotion (`demo 4`)
`A` is rooted, `junk1/junk2` are not. Press **Minor GC** repeatedly. **Expected:** `junk` dies in the first minor GC; A goes
Eden (age 0) → Survivor A (age 1) → Survivor B (age 2) → Old (promoted, because its age reached the threshold of 2).

### Demo 5 — Remembered set (`demo 5`)
An object `O` has been promoted to Old; a new young `Y` was then stored into it, and *nothing else references Y*. Look at the
barrier panel (the write barrier recorded `O` in the remembered set), then press **Minor GC**. **Expected:** `Y` survives
only because the remembered set made `O` an extra root.

### Bonus: Demo 6 — Copying (`demo 6`)
`A, x, B, y, C` in from-space; run GC and watch the live three be copied contiguously into to-space.

## What next?

Read [memory-model.md](memory-model.md), then follow the table in [README.md](README.md).
