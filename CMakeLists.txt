cmake_minimum_required(VERSION 3.16)
project(HeapLens VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# The heap is a raw byte buffer that we reinterpret as object headers, so keep
# the optimiser from making aliasing assumptions about it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -fno-strict-aliasing)
elseif(MSVC)
  add_compile_options(/W3 /EHsc)
endif()

# ---- core library: the managed runtime, GC algorithms, event stream, viz model
file(GLOB MINIGC_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/core/*.cpp)
add_library(minigc_core STATIC ${MINIGC_CORE_SOURCES})
target_include_directories(minigc_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ---- executable: CLI REPL + built-in HTTP server for the browser UI
add_executable(minigc src/main.cpp src/server.cpp)
target_link_libraries(minigc PRIVATE minigc_core)
target_compile_definitions(minigc PRIVATE MINIGC_UI_DIR="${CMAKE_SOURCE_DIR}/ui")
if(WIN32)
  target_link_libraries(minigc PRIVATE ws2_32)
endif()

# ---- tests (tiny in-repo framework, no external dependency)
enable_testing()
file(GLOB MINIGC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(minigc_tests ${MINIGC_TEST_SOURCES})
target_link_libraries(minigc_tests PRIVATE minigc_core)
target_include_directories(minigc_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# One ctest entry per suite; the suite name is the prefix before the first '.'
foreach(suite heap allocator objects graph refcount marksweep compact copying generational barrier safepoint events session)
  add_test(NAME ${suite} COMMAND minigc_tests ${suite}.)
endforeach()
