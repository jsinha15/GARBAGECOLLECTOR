// A deliberately tiny, single-threaded HTTP/1.1 server (raw sockets, no
// libraries) that serves the browser UI and exposes the runtime as a JSON API:
//
//   GET /                      -> ui/index.html (and other static UI files)
//   GET /api/state             -> snapshot of the runtime
//   GET /healthz               -> "ok" (for hosting platforms)
//   GET /api/demos             -> list of predefined scenarios
//   GET /api/cmd?c=<command>   -> run one command, returns before/events/after
#pragma once
#include <string>

#include "minigc/commands.h"

// host: "127.0.0.1" (local only) or "0.0.0.0" (reachable from other machines, e.g. when deployed).
int runServer(minigc::Session& session, const std::string& host, int port, const std::string& uiDir);
