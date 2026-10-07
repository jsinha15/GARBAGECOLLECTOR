# Building, testing and running HeapLens

**Short answer: you only need a C++17 compiler. CMake is optional.** HeapLens has no third-party
dependencies — the test framework, the HTTP server and the UI are all in this repository — and every
`.cpp` file is plain standard C++. CMake just saves typing; the one-liners below do the same job.

After building, **you never need the command line again**: start the server once and do everything
(write code, step through garbage collections, create objects, load demos) in the browser.

## 1. Prerequisites

| Need | Requirement | Notes |
|---|---|---|
| Operating system | Windows 10/11, Linux or macOS | |
| C++ compiler | **C++17**: GCC ≥ 9, Clang ≥ 10, or MSVC 2019+ (with the *C++ workload*, i.e. the standard-library headers and Windows SDK) | `g++ --version` |
| CMake | **optional**, ≥ 3.16 | only for the CMake route |
| Browser | any current browser | to use the UI |
| Node / npm / package managers | **not required** | Node is only used by the optional UI unit tests |

## 2. Build — pick ONE route

### A. Compiler only (no CMake)

**Linux / macOS / MSYS2 / MinGW** — use the script (it just runs your compiler):

```bash
./build.sh              # builds build/minigc and build/minigc_tests
./build.sh test         # ...and runs the tests
CXX=clang++ ./build.sh  # choose another compiler
```

or the equivalent single command:

```bash
mkdir -p build
g++ -std=c++17 -O1 -fno-strict-aliasing -Iinclude -DMINIGC_UI_DIR='"'$PWD/ui'"' \
    src/core/*.cpp src/main.cpp src/server.cpp -o build/minigc
# MinGW / MSYS2 on Windows: append  -lws2_32
```

**Windows with Visual Studio (no CMake)** — from a normal terminal:

```bat
build_msvc.bat          :: builds build\minigc.exe and build\minigc_tests.exe
build_msvc.bat test     :: ...and runs the tests
```

(or by hand, from an *x64 Native Tools Command Prompt*:
`cl /std:c++17 /EHsc /Iinclude src\core\*.cpp src\main.cpp src\server.cpp ws2_32.lib /Fe:build\minigc.exe`)

### B. CMake

```bash
cmake -S . -B build
cmake --build build                       # Windows/Visual Studio: add  --config Debug
ctest --test-dir build --output-on-failure   # Windows: add  -C Debug
```

Executables are in `build/` (Visual Studio generator: `build\Debug\`).

## 3. Run the visualization (this is all you need)

```bash
./build/minigc --serve                  # Windows: build\minigc.exe --serve
```

Open **http://127.0.0.1:8080/**. The header shows a green **connected** pill. Pick an example in the
**Code** tab and press **Step**. (`--port 9000` changes the port; the server listens on `127.0.0.1` only.)

## 4. Tests (optional)

C++ tests: `./build/minigc_tests` (all) or `./build/minigc_tests generational.` (name prefix).
UI tests (need Node): `node ui/dev/test-model.js && node ui/dev/test-script.js`.

## 5. Optional command-line mode

The runtime also has a terminal interface (`./build/minigc`, `--demo 1 --trace`) — handy for scripting and
debugging, but nothing requires it. See [docs/getting-started.md](docs/getting-started.md).
