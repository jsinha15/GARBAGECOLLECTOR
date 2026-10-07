// A ~40 line test framework so the project builds with no external packages.
//   TEST(suite.name) { CHECK(cond); CHECK_EQ(a, b); }
// Run:  minigc_tests              all tests
//       minigc_tests heap.        tests whose name starts with "heap."
#pragma once
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
  ++failures();
  std::cout << "    FAILED " << file << ":" << line << "  " << msg << "\n";
}

template <class A, class B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
  if (!(a == b)) {
    std::ostringstream o;
    o << ea << " == " << eb << "   (" << a << " vs " << b << ")";
    fail(file, line, o.str());
  }
}

}  // namespace testfw

// The test name is a string literal "suite.case".
#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
// __COUNTER__ (expanded once, as a macro argument) is robust even where __LINE__
// is not a plain constant (e.g. MSVC with /ZI).
#define TEST_IMPL(NAME, N) \
  static void TEST_CAT(test_fn_, N)(); \
  static testfw::Registrar TEST_CAT(test_reg_, N)(NAME, TEST_CAT(test_fn_, N)); \
  static void TEST_CAT(test_fn_, N)()
#define TEST(NAME) TEST_IMPL(NAME, __COUNTER__)

#define CHECK(cond) do { if (!(cond)) testfw::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_EQ(a, b) testfw::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
