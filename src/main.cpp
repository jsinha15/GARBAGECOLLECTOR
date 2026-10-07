// minigc - command line front end.
//
//   minigc                         interactive prompt
//   minigc --script file.txt       run commands from a file
//   minigc --demo 1 [--algo ms]    load a predefined scenario, then continue interactively
//   minigc --trace                 print every GC event (with its explanation)
//   minigc --serve [--port 8080]   start the browser UI
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "minigc/commands.h"
#include "minigc/events.h"
#include "minigc/visualization.h"
#include "server.h"

#ifndef MINIGC_UI_DIR
#define MINIGC_UI_DIR "ui"
#endif

#ifdef _WIN32
#include <io.h>
#define MINIGC_ISATTY(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#define MINIGC_ISATTY(f) isatty(fileno(f))
#endif

static void usage() {
  std::cout <<
      "HeapLens - a learning-oriented garbage collector laboratory\n\n"
      "usage: minigc [options]\n"
      "  --algo <rc|marksweep|compact|copying|generational>   initial collector (default marksweep)\n"
      "  --demo <1-6>             load a predefined scenario\n"
      "  --script <file>          run commands from a file, then exit\n"
      "  --trace                  print each GC event with its explanation\n"
      "  --log <error|warn|info|debug>   runtime log level (stderr)\n"
      "  --serve                  start the HTTP server for the browser UI\n"
      "  --port <n>               server port (default 8080, or $PORT)\n"
      "  --host <addr>            bind address (default 127.0.0.1; use 0.0.0.0 to accept remote connections)\n"
      "  --ui <dir>               directory containing the UI files\n"
      "  --help\n\n"
      "Type `help` at the prompt for the command language.\n";
}

int main(int argc, char** argv) {
  minigc::Config cfg;
  bool serve = false, trace = false;
  int port = 8080, demo = 0;
  std::string script, uiDir = MINIGC_UI_DIR, host;
  // Hosting platforms pass the port in $PORT and need the server reachable from outside.
  if (const char* envPort = std::getenv("PORT")) {
    port = std::atoi(envPort);
    host = "0.0.0.0";
  }

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(2); }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") { usage(); return 0; }
    else if (a == "--serve") serve = true;
    else if (a == "--trace") trace = true;
    else if (a == "--port") port = std::atoi(next().c_str());
    else if (a == "--host") host = next();
    else if (a == "--ui") uiDir = next();
    else if (a == "--script") script = next();
    else if (a == "--demo") demo = std::atoi(next().c_str());
    else if (a == "--algo") {
      if (!minigc::parseAlgo(next(), cfg.algo)) { std::cerr << "unknown algorithm\n"; return 2; }
    } else if (a == "--log") {
      std::string l = next();
      minigc::LogLevel lv = l == "error" ? minigc::LogLevel::Error : l == "warn" ? minigc::LogLevel::Warn
                          : l == "info" ? minigc::LogLevel::Info : minigc::LogLevel::Debug;
      minigc::Logger::get().setLevel(lv);
    } else { std::cerr << "unknown option " << a << "\n"; usage(); return 2; }
  }

  minigc::Session session(cfg);
  if (host.empty()) host = "127.0.0.1";
  if (serve) return runServer(session, host, port, uiDir);

  auto execute = [&](const std::string& line) {
    minigc::CommandResult r = session.run(line);
    if (trace)
      for (size_t i = r.eventStart; i < r.eventEnd; ++i)
        std::cout << "  " << minigc::describeEvent(session.runtime().events.history()[i]) << "\n";
    if (!r.message.empty()) std::cout << (r.ok ? "" : "! ") << r.message << "\n";
    return r.ok;
  };

  if (demo > 0) execute("demo " + std::to_string(demo));

  if (!script.empty()) {
    std::ifstream in(script);
    if (!in) { std::cerr << "cannot open " << script << "\n"; return 1; }
    std::string line;
    bool ok = true;
    while (std::getline(in, line)) {
      std::cout << "> " << line << "\n";
      ok = execute(line) && ok;
    }
    return ok ? 0 : 1;
  }

  bool tty = MINIGC_ISATTY(stdin) != 0;
  if (tty) std::cout << "HeapLens (" << minigc::algoName(cfg.algo) << "). Type `help`, or `quit` to exit.\n";
  std::string line;
  for (;;) {
    if (tty) std::cout << "minigc> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    if (line == "quit" || line == "exit") break;
    execute(line);
  }
  return 0;
}
