// Helpers shared by the test suites.
#pragma once
#include <set>
#include <string>
#include <vector>

#include "minigc/commands.h"
#include "minigc/graph.h"
#include "minigc/runtime.h"
#include "test_framework.h"

namespace tu {

using namespace minigc;

inline Config cfgFor(Algo a, uint32_t heapBytes = 2048) {
  Config c;
  c.algo = a;
  c.heapBytes = heapBytes;
  return c;
}

inline ObjId node(Runtime& rt, const std::string& label, uint32_t refs = 2) {
  return rt.allocate("Node", refs, 0, label);
}

inline Addr addr(Runtime& rt, const std::string& label) { return rt.addrOf(rt.resolve(label)); }
inline bool alive(Runtime& rt, const std::string& label) { return rt.resolve(label) != 0; }
inline ObjectHeader& hdr(Runtime& rt, const std::string& label) { return rt.header(rt.resolve(label)); }
inline std::string regionOf(Runtime& rt, const std::string& label) { return rt.spaceName(addr(rt, label)); }

// Target object id stored in a reference slot (0 = null).
inline ObjId target(Runtime& rt, const std::string& holder, uint32_t slot) {
  Addr a = addr(rt, holder);
  Addr t = rt.heap.mem().refs(a)[slot];
  return t ? rt.heap.mem().header(t)->id : 0;
}

inline Addr rawTarget(Runtime& rt, const std::string& holder, uint32_t slot) {
  return rt.heap.mem().refs(addr(rt, holder))[slot];
}

inline size_t count(const Runtime& rt, EventType t) {
  size_t n = 0;
  for (const Event& e : rt.events.history())
    if (e.type == t) ++n;
  return n;
}

inline std::vector<EventType> types(const Runtime& rt, size_t from = 0) {
  std::vector<EventType> out;
  for (size_t i = from; i < rt.events.history().size(); ++i) out.push_back(rt.events.history()[i].type);
  return out;
}

// True if `order` appears as a (not necessarily contiguous) subsequence of the event types.
inline bool containsInOrder(const Runtime& rt, const std::vector<EventType>& order) {
  size_t next = 0;
  for (const Event& e : rt.events.history())
    if (next < order.size() && e.type == order[next]) ++next;
  return next == order.size();
}

// Structural invariants that must hold after ANY operation.
inline bool consistent(Runtime& rt, std::string* why = nullptr) {
  auto bad = [&](const std::string& m) { if (why) *why = m; return false; };
  Memory& mem = rt.heap.mem();
  size_t walked = 0;
  for (auto& rp : rt.heap.regions()) {
    Region& r = *rp;
    Addr a = r.start;
    Addr limit = r.alloc->walkLimit();
    while (a < limit) {
      ObjectHeader* h = mem.header(a);
      if (h->size == 0 || h->size % 8 != 0) return bad("bad block size at " + hex(a));
      if (a + h->size > r.end) return bad("block overruns region at " + hex(a));
      if (h->id != 0) {
        ++walked;
        if (!rt.exists(h->id)) return bad("object " + std::to_string(h->id) + " missing from object table");
        if (rt.addrOf(h->id) != a) return bad("object table address mismatch for " + std::to_string(h->id));
        if (h->forward != 0) return bad("stale forwarding pointer on " + std::to_string(h->id));
      }
      a += h->size;
    }
    if (a != limit) return bad("blocks do not tile region " + r.name);
  }
  if (walked != rt.objects().size()) return bad("table has " + std::to_string(rt.objects().size()) + " objects, heap walk found " + std::to_string(walked));
  for (const auto& kv : rt.objects()) {
    ObjectHeader* h = mem.header(kv.second);
    for (uint32_t i = 0; i < h->numRefs; ++i) {
      Addr t = mem.refs(kv.second)[i];
      if (t == kNull) continue;
      if (!rt.heap.isObject(t)) return bad("dangling reference in object " + std::to_string(kv.first));
      if (rt.addrOf(mem.header(t)->id) != t) return bad("stale reference in object " + std::to_string(kv.first));
    }
  }
  for (const Root& root : rt.roots.all()) {
    if (root.addr == kNull) continue;
    if (!rt.heap.isObject(root.addr)) return bad("dangling root " + root.name);
    if (rt.addrOf(rt.heap.mem().header(root.addr)->id) != root.addr) return bad("stale root " + root.name);
  }
  return true;
}

#define CHECK_CONSISTENT(rt) do { std::string why_; if (!tu::consistent(rt, &why_)) testfw::fail(__FILE__, __LINE__, "heap inconsistent: " + why_); } while (0)

// Deterministic pseudo-random object graph: n objects, random edges, some roots.
inline void buildRandomGraph(Runtime& rt, uint32_t seed, int n, int rootEvery = 4) {
  uint32_t s = seed;
  auto rnd = [&](uint32_t mod) { s = s * 1664525u + 1013904223u; return (s >> 8) % mod; };
  std::vector<ObjId> ids;
  for (int i = 0; i < n; ++i) ids.push_back(rt.allocate("Node", 2, 0, "n" + std::to_string(i)));
  for (int i = 0; i < n; ++i)
    for (uint32_t slot = 0; slot < 2; ++slot)
      if (rnd(3) != 0) rt.setRef(ids[i], slot, ids[rnd(static_cast<uint32_t>(n))]);
  for (int i = 0; i < n; i += rootEvery) rt.addRoot("r" + std::to_string(i), ids[i]);
}

inline std::set<ObjId> allIds(Runtime& rt) {
  std::set<ObjId> out;
  for (const auto& kv : rt.objects()) out.insert(kv.first);
  return out;
}

}  // namespace tu
