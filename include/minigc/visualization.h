// The visualization model. This layer only READS the runtime (heap snapshots)
// and the event stream, and turns them into JSON / text for the UI and CLI.
// The GC algorithms never call into it.
#pragma once
#include <string>
#include <vector>

#include "minigc/events.h"
#include "minigc/runtime.h"

namespace minigc {

std::string jsonEscape(const std::string& s);
std::string eventToJson(const Event& e);
// Events history[from, to) as a JSON array.
std::string eventsToJson(const std::vector<Event>& history, size_t from, size_t to);
// The complete visible state of the runtime: regions, objects, roots, free
// blocks, remembered set, card table, safepoint state, statistics.
std::string snapshotJson(Runtime& rt);

// Plain-text renderings for the CLI.
std::string asciiHeap(Runtime& rt);
std::string describeObject(Runtime& rt, ObjId id);
std::string describeEvent(const Event& e);

}  // namespace minigc
