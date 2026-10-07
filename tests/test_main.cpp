#include <exception>

#include "test_framework.h"

int main(int argc, char** argv) {
  std::string prefix = argc > 1 ? argv[1] : "";
  int ran = 0, failedTests = 0;
  for (auto& t : testfw::registry()) {
    if (t.name.compare(0, prefix.size(), prefix) != 0) continue;
    ++ran;
    int before = testfw::failures();
    std::cout << "[ RUN  ] " << t.name << "\n";
    try {
      t.fn();
    } catch (const std::exception& e) {
      testfw::fail(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
    }
    if (testfw::failures() != before) {
      ++failedTests;
      std::cout << "[ FAIL ] " << t.name << "\n";
    }
  }
  std::cout << "\n" << ran << " test(s) run, " << failedTests << " failed.\n";
  if (ran == 0) { std::cout << "no tests matched '" << prefix << "'\n"; return 1; }
  return failedTests == 0 ? 0 : 1;
}
