#!/bin/sh
# Build HeapLens with just a C++17 compiler - no CMake needed.
#   ./build.sh            builds build/minigc and build/minigc_tests
#   ./build.sh test       ...and runs the tests
#   CXX=clang++ ./build.sh   to pick another compiler
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
mkdir -p build

EXTRA=""
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXTRA="-lws2_32" ;;   # Winsock on Windows (MinGW / MSYS2)
esac

FLAGS="-std=c++17 -O1 -fno-strict-aliasing -Wall -Wextra -Iinclude"

echo "Compiling minigc ..."
$CXX $FLAGS "-DMINIGC_UI_DIR=\"$(pwd)/ui\"" src/core/*.cpp src/main.cpp src/server.cpp -o build/minigc $EXTRA

echo "Compiling tests ..."
$CXX $FLAGS -Itests src/core/*.cpp tests/*.cpp -o build/minigc_tests

echo
echo "Built build/minigc and build/minigc_tests"
echo "Start the UI:  ./build/minigc --serve     then open http://127.0.0.1:8080/"
if [ "$1" = "test" ]; then ./build/minigc_tests; fi
