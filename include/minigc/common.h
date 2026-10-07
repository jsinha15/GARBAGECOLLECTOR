// Shared vocabulary types used by every HeapLens component.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace minigc {

// A heap address. The managed heap is simulated: addresses start at
// Config::heapBase (0x1000) so that 0 can safely mean "null".
using Addr = uint32_t;
using ObjId = uint32_t;
constexpr Addr kNull = 0;
constexpr uint32_t kAlign = 8;

inline uint32_t alignUp(uint32_t n, uint32_t a = kAlign) { return (n + a - 1) / a * a; }

inline std::string hex(Addr a) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%04X", static_cast<unsigned>(a));
  return buf;
}

enum class Algo { RefCount, MarkSweep, MarkCompact, Copying, Generational };

inline const char* algoName(Algo a) {
  switch (a) {
    case Algo::RefCount: return "Reference Counting";
    case Algo::MarkSweep: return "Mark-Sweep";
    case Algo::MarkCompact: return "Mark-Compact";
    case Algo::Copying: return "Copying";
    case Algo::Generational: return "Generational";
  }
  return "?";
}

inline const char* algoKey(Algo a) {
  switch (a) {
    case Algo::RefCount: return "rc";
    case Algo::MarkSweep: return "marksweep";
    case Algo::MarkCompact: return "compact";
    case Algo::Copying: return "copying";
    case Algo::Generational: return "generational";
  }
  return "?";
}

inline bool parseAlgo(const std::string& s, Algo& out) {
  if (s == "rc" || s == "refcount") { out = Algo::RefCount; return true; }
  if (s == "ms" || s == "marksweep") { out = Algo::MarkSweep; return true; }
  if (s == "mc" || s == "compact" || s == "markcompact") { out = Algo::MarkCompact; return true; }
  if (s == "copy" || s == "copying") { out = Algo::Copying; return true; }
  if (s == "gen" || s == "generational") { out = Algo::Generational; return true; }
  return false;
}

// Which collection to run. Only the generational collector distinguishes them.
enum class CollectKind { Auto, Minor, Major };

}  // namespace minigc
