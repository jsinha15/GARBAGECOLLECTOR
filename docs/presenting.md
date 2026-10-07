# Presenting HeapLens: how to show it and run the demos

Everything here assumes Windows with the compiler at `C:\mingw64` (see [../BUILD.md](../BUILD.md)). On other systems
replace step 1 with `./build.sh && ./build/minigc --serve`.

## 0. Pre-flight checklist (5 minutes before)

| ✔ | Do this | Why |
|---|---|---|
| ☐ | Double-click **`start.bat`** (first run compiles, ~30 s). A browser opens at http://127.0.0.1:8080/ | one-click start |
| ☐ | Top-right pill says **connected** | the backend is up |
| ☐ | Browser zoom to **110–125 %**, window maximised, hide bookmarks bar | readable from the back of the room |
| ☐ | Press **? Guide** to show the 3-step strip (it hides itself after you dismiss it) | audience orientation |
| ☐ | Run **Skip to end** on example 1, then **Restart** | warms everything up and proves it works |
| ☐ | Put the speed slider in the middle; keep **Step** for explaining, **Play** only for recap | you control the pace |
| ☐ | Optional: open this guide and `docs/learning-guide.md` on a second screen | notes |
| ☐ | Dark room/projector? Your OS dark mode switches the UI to its dark theme automatically | contrast |

If the port is busy: `start.bat -Port 9000` (or `.\start.ps1 -Port 9000`).

## 1. The one-sentence pitch
> "A garbage collector is the part of a runtime that frees memory your program can no longer reach. I built five of them from
> scratch in C++ — and a screen that lets you watch each one think, one step at a time, with the reason for every step."

## 2. A 10-minute talk (any audience)

| Min | What you do | What you say |
|---|---|---|
| 0–1 | Show the UI; point at **code (left)** and **memory (right)**. | "Left is a tiny program. Right is the real memory of my runtime. Every variable you create is a *root*." |
| 1–3 | Example **1. Basic garbage**. Press **Step** 9× until `gc()`. | "`c` and `d` are gone as variables, but their objects still sit in memory — red, *no variable can reach it*." |
| 3–6 | Press **Step** through the collection. Pause on *ROOT SCAN*, *MARK*, *SWEEP*. Click an object for the *why*. | "Mark follows references from roots. Gray = found, black = scanned. Whatever stays white is garbage. Not because it's old — because **no path exists**." |
| 6–8 | Example **2. Cycle** (reference counting). Run it; show the cycle alert; change line 2 to `use marksweep`, **Restart**, run. | "Counting can't free a cycle — each object keeps the other alive. A tracer frees it." |
| 8–10 | Example **7. Aging and promotion** with **Play**. | "Most objects die young, so we collect a small young area often and promote survivors to Old." |

## 3. A 25-minute talk (technical audience)
Add after minute 10:

| Min | Demo | Key point | Where to click |
|---|---|---|---|
| 10–14 | Example **5. Compaction** | Sweeping leaves holes (see the `FREE` gaps, fragmentation %). Compaction slides objects and **must rewrite every reference**, even in objects that did not move. | Step to the 2nd `gc()`; on *UPDATE REFERENCES* read the orange card: "old reference → new reference". |
| 14–17 | Example **6. Copying** | From-space → to-space with forwarding pointers; fragmentation can't occur; cost ∝ live data. | Watch the `FROM-SPACE`/`TO-SPACE` rows swap names. |
| 17–21 | Example **8. Write barrier** | Why a minor GC needs the remembered set. | **Details** tab: the *WRITE → BARRIER → REMEMBERED SET* diagram appears at `old.next = young`. |
| 21–23 | **Events** tab | "The UI is built *only* from this event stream; collectors never draw." | Scroll the stream during a GC. |
| 23–25 | Architecture + tests | Diagram from README; run `build\minigc_tests.exe` in a terminal (120 tests). | see §6 |

## 4. Click-by-click demo scripts

Every number below was produced by running the real runtime.

### Demo A — Basic garbage (example 1)
1. Select **1. Basic garbage**. Narrate the code: *a→b, c→d, then drop c and d*.
2. **Step** ×9 reaches `gc()`. **Variables & objects**: `a`, `b` REACHABLE; `c`, `d` UNREACHABLE.
3. **Step** into the GC. The blue bar shows *step n of 25*. Say each phase aloud (*GC START, ROOT SCAN, MARK, SWEEP*).
4. At *SWEEP* point at the two hatched/struck-through blocks: RECLAIMED.
5. **Expected end state:** 2 objects (`a`, `b`) remain; `c` and `d` reclaimed.

### Demo B — Cycle (example 2)
1. Run with **Skip to end**. Show objects `a` and `b` still in the table, status *UNREACHABLE*; **Details** tab shows the cycle alert and
   refCount 1/1.
2. Edit line 2 `use rc` → `use marksweep`, **Restart**, **Skip to end**: table is empty.
3. **Message:** the same program, a different collector, different result.

### Demo C — Compaction (example 5)
1. **Step** to the first `gc()` and through it: sweep creates `FREE` holes where `x` and `y` were.
2. Continue to `use compact`; point out the **Collector** dropdown changed to Mark-Compact and the heap is unchanged ("same heap, new collector").
3. Step through the second `gc()`: *COMPUTE ADDRESSES* (blocks show WILL MOVE / STAYS), *UPDATE REFERENCES*, *MOVE OBJECTS*.
4. **Expected:** `[a][b][c][ FREE… ]`; `b` and `c` moved down one slot each (e.g. `0x1040 → 0x1020`), `a` stayed.

### Demo D — Generational promotion (example 7)
1. **Step** to the first `gc(minor)`. *keeper* moves **Eden → Survivor A**, age 1; *junk* is reclaimed.
2. Next: *Survivor B*, age 2. Next: **Old**, "PROMOTED because its age (2) reached the promotion threshold (2)".
3. Fourth minor GC: nothing happens to Old objects.

### Demo E — Remembered set (example 8)
1. Three `gc(minor)` lines promote `old`. 
2. At `old.next = young` open **Details**: the barrier diagram fires; the remembered set lists `old`.
3. `young = null`, then `gc(minor)`: `young` **survives**. Ask the audience *why* before revealing.

### Demo F — Automatic GC (example 9)
Press **Skip to end** (no `gc()` in the code). Open **Details → Statistics**: collections > 0 — Eden filled and minor GCs ran by themselves.

### Demo G — Live coding (best for engagement)
Pick **10. Empty sandbox** and type with the audience:
```
use marksweep
a = new Node(next, other)
b = new Node
a.next = b
b.next = a        // a cycle!
a = null
b = null
gc()
```
Ask: "Will the cycle be freed?" (Yes under marksweep; change to `use rc` and repeat: no.)

## 5. Presenting the architecture (one slide)
Draw or show the diagram from the README:

```
program (variables, objects)  ──►  Runtime  ──►  Heap + Allocators
                                      │
                           Collector (RC | MS | MC | Copying | Generational)
                                      │ emits events
                          Event stream ──► Logger / Metrics / Browser UI
```
Talking points: *collectors are swappable behind one interface; the allocator doesn't know about GC; the UI doesn't know about the
collector — only events.* Then: "which is why every demo is deterministic and every behaviour has a test."

## 6. Showing the code and tests
* Open `src/core/gc_marksweep.cpp` (≈70 lines) — the shortest complete collector; read the `emit` lines aloud.
* Open `src/core/gc_markcompact.cpp` — point at the four phase comments.
* Run the tests in a terminal: `cd D:\Practice\GARBAGECOLLECTOR` then `.\build\minigc_tests.exe` → "120 test(s) run, 0 failed."
  Run one suite: `.\build\minigc_tests.exe generational.`
* Show one test that proves a claim: `barrier.without_the_barrier_the_young_object_would_be_lost`.

## 7. Questions you will be asked
| Question | Short answer |
|---|---|
| Is this a real JVM GC? | No — a teaching lab that implements the real *algorithms* at small scale (2 KB heap) so every step can be seen. |
| Why is the heap so small? | So everything fits on screen; the `Heap` selector goes up to 4 KB, and `set heap N` in your program up to 64 KB. |
| Why simulated threads? | Concepts (safepoints, stop-the-world) without OS scheduling noise; keeps runs deterministic. |
| Concurrent / parallel GC? | Out of scope on purpose; stop-the-world only. |
| How do you know the collectors are correct? | 120 tests incl. comparing every collector's survivors with an independent reachability analysis on random graphs, plus a heap-consistency check after every GC. |
| What would you add next? | Concurrent marking (needs a snapshot/incremental barrier), weak references, adaptive promotion. |

## 8. If something goes wrong
| Symptom | Fix |
|---|---|
| Pill says *server not reachable* | `start.bat` window closed. Run it again. |
| Page shows old behaviour | Hard-refresh: **Ctrl+F5**. |
| Port in use | `start.bat -Port 9000`, open http://127.0.0.1:9000/ |
| Buttons greyed out | A GC replay is on screen: press **Skip** (or **Next** until it ends). |
| Program shows a red line | Read the message under the editor (compile or runtime error), fix, **Restart**. |
| Want a clean slate | **Restart**. |
| Build fails on a fresh machine | `docs/troubleshooting.md`, `BUILD.md`. Have `build\minigc.exe` pre-built as a backup. |

## 9. Leave-behind
Send people: the repo, `README.md` (quick start), and `docs/learning-guide.md` (self-study path with exercises).
