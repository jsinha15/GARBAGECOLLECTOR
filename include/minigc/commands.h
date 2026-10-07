// A Session wraps a Runtime with a tiny text command language. The CLI, the
// HTTP server (and therefore the browser UI) and the tests all drive the
// runtime through it, which keeps every scenario reproducible and scriptable.
#pragma once
#include <string>
#include <vector>

#include "minigc/runtime.h"

namespace minigc {

struct DemoInfo {
  int number;
  std::string title;
  std::string algo;                 // algorithm key the demo starts with
  std::string description;
  std::string next;                 // what the learner should do next
  std::vector<std::string> script;  // setup commands
};

struct CommandResult {
  bool ok = true;
  std::string message;
  bool replay = false;       // the command ran a GC: the UI should offer step-by-step replay
  size_t eventStart = 0;     // events produced: history[eventStart, eventEnd)
  size_t eventEnd = 0;
};

class Session {
 public:
  explicit Session(const Config& cfg = Config());

  Runtime& runtime() { return rt_; }

  CommandResult run(const std::string& line);
  // run() + JSON: {"ok","command","message","replay","before","events","after"}
  std::string runJson(const std::string& line);
  std::string stateJson();

  static const std::vector<DemoInfo>& demos();
  static std::string help();
  static std::string demosJson();

 private:
  CommandResult exec(const std::vector<std::string>& tok);
  CommandResult runDemo(int number, const std::string& algoOverride);

  Runtime rt_;
};

}  // namespace minigc
