# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `cmake: command not found` | Install CMake ≥ 3.16 ([BUILD.md](../BUILD.md)). Windows without CMake: use `build_msvc.bat`. |
| Compiler error about `std::filesystem`/C++17 features | Your compiler is too old: need GCC ≥ 9, Clang ≥ 10 or MSVC 2019+. |
| MSVC: `cannot open include file 'vector'` | The Visual Studio *C++ workload* (headers + Windows SDK) is not installed; add it in the VS installer. |
| Windows link errors for `socket`, `bind`… | `ws2_32` is linked by CMake / `build_msvc.bat`; if you build by hand add `ws2_32.lib` (MinGW: `-lws2_32`). |
| `Cannot listen on 127.0.0.1:8080` | Port in use. `./build/minigc --serve --port 9000`. |
| UI pill says **server not reachable** | The backend is not running, or you opened `index.html` as a file. Start `minigc --serve` and use `http://127.0.0.1:<port>/`. |
| `Cannot find the UI directory` | Run from the repository root or pass `--ui <repo>/ui`. |
| `ctest` says *Test not available without configuration* (Windows) | Add `-C Debug` (or the configuration you built). |
| UI buttons are greyed out | A GC replay is in progress: press **Finish**, or **Step** until it ends. |
| "Out of memory" while creating objects | The 2 KB heap is full: run a GC, root fewer objects, or `set heap 4096`. Large objects (> 256 B) in generational mode go to Old (768 B). |
| Switching collector wiped my objects | Only `rc`, `marksweep`, `compact` share a layout and keep the heap; `copying`/`generational` reset it. |
| Reference counting did not free my cycle | Correct — that is the demonstration ([reference-counting.md](reference-counting.md)). Try `algo marksweep`. |
| A young object survived although nothing references it | An *old* object that is itself garbage still keeps its young targets alive during minor GCs; run `gc major`. |
| A promotion failure error | The old generation is full or fragmented during a GC (`gc major` first, or enlarge the heap). The heap may be partially collected; `reset` to start clean. |
| Demo says "Demo setup failed" | Report it — demos are covered by `session.every_demo_survives_every_collection_kind`. |

Debugging aids: `minigc --log debug` (event log on stderr), `minigc --trace`, the `heap`, `status`, `events`, `inspect`, `why` commands, and
`CHECK_CONSISTENT(rt)` / `tu::consistent()` in tests (walks every region and validates every reference).
