#include "minigc/visualization.h"

#include <cstdio>
#include <sstream>

#include "minigc/graph.h"

namespace minigc {

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

static std::string q(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

std::string eventToJson(const Event& e) {
  std::ostringstream o;
  o << "{\"seq\":" << e.seq << ",\"type\":" << q(eventName(e.type)) << ",\"id\":" << e.id
    << ",\"id2\":" << e.id2 << ",\"addr\":" << e.addr << ",\"addr2\":" << e.addr2
    << ",\"value\":" << e.value << ",\"phase\":" << q(e.phase) << ",\"text\":" << q(e.text) << "}";
  return o.str();
}

std::string eventsToJson(const std::vector<Event>& history, size_t from, size_t to) {
  std::ostringstream o;
  o << "[";
  if (to > history.size()) to = history.size();
  for (size_t i = from; i < to; ++i) {
    if (i != from) o << ",";
    o << eventToJson(history[i]);
  }
  o << "]";
  return o.str();
}

static const char* allocKind(Region& r) { return r.alloc->kind(); }

std::string snapshotJson(Runtime& rt) {
  Memory& mem = rt.heap.mem();
  ObjectGraph graph(rt);
  std::set<ObjId> live = graph.reachable();
  std::ostringstream o;
  o << "{";

  // ---- config
  const Config& c = rt.config;
  o << "\"algo\":" << q(algoKey(c.algo)) << ",\"algoName\":" << q(algoName(c.algo))
    << ",\"config\":{\"heapBase\":" << c.heapBase << ",\"heapBytes\":" << c.heapBytes
    << ",\"promotionThreshold\":" << c.promotionThreshold << ",\"cardBytes\":" << c.cardBytes
    << ",\"useCardTable\":" << (c.useCardTable ? "true" : "false")
    << ",\"autoGC\":" << (c.autoGC ? "true" : "false") << ",\"threads\":" << c.threads << "}";

  // ---- regions + free blocks
  o << ",\"regions\":[";
  std::ostringstream freeBlocks;
  bool firstFree = true;
  bool firstRegion = true;
  for (auto& rp : rt.heap.regions()) {
    Region& r = *rp;
    RegionStats s = rt.heap.regionStats(r);
    if (!firstRegion) o << ",";
    firstRegion = false;
    o << "{\"kind\":" << q(regionKindName(r.kind)) << ",\"name\":" << q(r.name) << ",\"start\":" << r.start
      << ",\"end\":" << r.end << ",\"alloc\":" << q(allocKind(r)) << ",\"top\":" << r.alloc->walkLimit()
      << ",\"used\":" << s.used << ",\"free\":" << s.free << ",\"objects\":" << s.objects
      << ",\"freeBlocks\":" << s.freeBlocks << ",\"largestFree\":" << s.largestFree
      << ",\"fragmentation\":" << static_cast<int>(s.fragmentation * 100 + 0.5) << "}";
    rt.heap.forEachBlock(r, [&](Addr a, ObjectHeader& h) {
      if (h.id != 0) return;
      if (!firstFree) freeBlocks << ",";
      firstFree = false;
      freeBlocks << "{\"addr\":" << a << ",\"size\":" << h.size << ",\"region\":" << q(r.name) << "}";
    });
    if (r.alloc->walkLimit() < r.end) {  // untouched tail of a bump region
      if (!firstFree) freeBlocks << ",";
      firstFree = false;
      freeBlocks << "{\"addr\":" << r.alloc->walkLimit() << ",\"size\":" << (r.end - r.alloc->walkLimit())
                 << ",\"region\":" << q(r.name) << "}";
    }
  }
  o << "],\"freeBlocks\":[" << freeBlocks.str() << "]";

  // ---- objects
  o << ",\"objects\":[";
  bool first = true;
  for (const auto& kv : rt.objects()) {
    ObjId id = kv.first;
    Addr a = kv.second;
    ObjectHeader* h = mem.header(a);
    Region* r = rt.heap.regionOf(a);
    if (!first) o << ",";
    first = false;
    o << "{\"id\":" << id << ",\"label\":" << q(rt.label(id)) << ",\"type\":" << q(rt.typeName(h->type))
      << ",\"addr\":" << a << ",\"size\":" << h->size << ",\"region\":" << q(r ? r->name : "?")
      << ",\"regionKind\":" << q(r ? regionKindName(r->kind) : "?") << ",\"age\":" << static_cast<int>(h->age)
      << ",\"color\":" << static_cast<int>(h->color) << ",\"refCount\":" << h->refCount
      << ",\"numRefs\":" << static_cast<int>(h->numRefs) << ",\"dataBytes\":"
      << (h->size - kHeaderSize - 4 * h->numRefs) << ",\"live\":" << (live.count(id) ? "true" : "false")
      << ",\"why\":" << q(graph.explain(id)) << ",\"refs\":[";
    for (uint32_t i = 0; i < h->numRefs; ++i) {
      Addr t = mem.refs(a)[i];
      if (i) o << ",";
      o << "{\"slot\":" << i << ",\"target\":" << (t ? mem.header(t)->id : 0) << ",\"addr\":" << t << "}";
    }
    o << "]}";
  }
  o << "]";

  // ---- roots
  o << ",\"roots\":[";
  first = true;
  for (const Root& r : rt.roots.all()) {
    if (!first) o << ",";
    first = false;
    o << "{\"id\":" << r.id << ",\"name\":" << q(r.name) << ",\"kind\":" << q(r.kind == RootKind::Global ? "global" : "stack")
      << ",\"thread\":" << r.thread << ",\"target\":" << (r.addr ? mem.header(r.addr)->id : 0)
      << ",\"addr\":" << r.addr << "}";
  }
  o << "]";

  // ---- generational structures
  o << ",\"remembered\":[";
  first = true;
  for (ObjId id : rt.barriers.remembered()) {
    if (!first) o << ",";
    first = false;
    o << id;
  }
  o << "]";
  const CardTable& cards = rt.barriers.cards();
  o << ",\"cards\":{\"start\":" << cards.start() << ",\"cardBytes\":" << cards.cardBytes()
    << ",\"count\":" << cards.cardCount() << ",\"dirty\":[";
  first = true;
  for (size_t i : cards.dirtyCards()) {
    if (!first) o << ",";
    first = false;
    o << i;
  }
  o << "]}";

  // ---- safepoints
  o << ",\"safepoint\":{\"state\":" << q(worldStateName(rt.safepoints.state())) << ",\"threads\":[";
  first = true;
  for (const SimThread& t : rt.safepoints.threads()) {
    if (!first) o << ",";
    first = false;
    o << "{\"index\":" << t.index << ",\"state\":"
      << q(t.state == ThreadState::Running ? "RUNNING" : "AT_SAFEPOINT") << ",\"ticksToPoll\":" << t.ticksToPoll << "}";
  }
  o << "]}";

  // ---- cycles (reference counting demo) and statistics
  o << ",\"cycles\":[";
  first = true;
  for (const auto& cyc : graph.unreachableCycles()) {
    if (!first) o << ",";
    first = false;
    o << "[";
    for (size_t i = 0; i < cyc.size(); ++i) o << (i ? "," : "") << cyc[i];
    o << "]";
  }
  o << "]";

  RegionStats hs = rt.heap.stats();
  o << ",\"stats\":{";
  first = true;
  for (const auto& kv : rt.metrics.toMap()) {
    if (!first) o << ",";
    first = false;
    o << q(kv.first) << ":" << kv.second;
  }
  o << ",\"heapSize\":" << hs.size << ",\"heapUsed\":" << hs.used << ",\"heapFree\":" << hs.free
    << ",\"liveObjects\":" << hs.objects << ",\"freeBlocks\":" << hs.freeBlocks
    << ",\"largestFree\":" << hs.largestFree
    << ",\"fragmentation\":" << static_cast<int>(hs.fragmentation * 100 + 0.5) << "}";
  o << "}";
  return o.str();
}

// ------------------------------------------------------------------ text views
std::string asciiHeap(Runtime& rt) {
  std::ostringstream o;
  Memory& mem = rt.heap.mem();
  for (auto& rp : rt.heap.regions()) {
    Region& r = *rp;
    o << r.name << "  [" << hex(r.start) << " - " << hex(r.end) << ")  " << allocKind(r) << "\n  ";
    rt.heap.forEachBlock(r, [&](Addr a, ObjectHeader& h) {
      if (h.id == 0) o << "[FREE " << h.size << "B]";
      else o << "[" << rt.label(h.id) << "#" << h.id << "@" << hex(a) << " " << h.size << "B]";
    });
    if (r.alloc->walkLimit() < r.end) o << "[FREE " << (r.end - r.alloc->walkLimit()) << "B]";
    o << "\n";
  }
  (void)mem;
  return o.str();
}

std::string describeObject(Runtime& rt, ObjId id) {
  if (!rt.exists(id)) return "No such object.";
  Memory& mem = rt.heap.mem();
  Addr a = rt.addrOf(id);
  ObjectHeader* h = mem.header(a);
  ObjectGraph g(rt);
  std::ostringstream o;
  o << rt.name(id) << "\n"
    << "  Type:       " << rt.typeName(h->type) << "\n"
    << "  Address:    " << hex(a) << "\n"
    << "  Size:       " << h->size << " bytes\n"
    << "  Space:      " << rt.spaceName(a) << "\n"
    << "  Age:        " << static_cast<int>(h->age) << "\n"
    << "  RefCount:   " << h->refCount << "\n"
    << "  References:";
  bool any = false;
  for (uint32_t i = 0; i < h->numRefs; ++i) {
    Addr t = mem.refs(a)[i];
    if (!t) continue;
    any = true;
    o << "\n    [" << i << "] -> " << rt.name(mem.header(t)->id) << " @ " << hex(t);
  }
  if (!any) o << " (none)";
  o << "\n  Referenced by:";
  any = false;
  for (ObjId other : g.nodes())
    for (ObjId t : g.edgesOf(other))
      if (t == id) { any = true; o << "\n    " << rt.name(other); }
  for (const Root& r : rt.roots.all())
    if (r.addr == a) { any = true; o << "\n    root '" << r.name << "'"; }
  if (!any) o << " (nothing)";
  o << "\n  Why:        " << (g.reachable().count(id) ? "LIVE - " : "UNREACHABLE - ") << g.explain(id);
  return o.str();
}

std::string describeEvent(const Event& e) {
  std::ostringstream o;
  o << "#" << e.seq << " " << eventName(e.type);
  if (!e.phase.empty()) o << " [" << e.phase << "]";
  if (!e.text.empty()) o << "  " << e.text;
  return o.str();
}

}  // namespace minigc
