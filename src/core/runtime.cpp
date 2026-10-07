#include "minigc/runtime.h"

#include <cctype>
#include <cstring>

#include "minigc/collector.h"

namespace minigc {

Runtime::Runtime(const Config& cfg)
    : config(cfg), heap(cfg.heapBase, cfg.heapBytes), barriers(events) {
  metrics.attach(events);
  reset();
}

Runtime::~Runtime() = default;

void Runtime::reset() {
  collector.reset();
  heap.clearRegions();
  heap.mem().zero(heap.base(), heap.size());
  table_.clear();
  labels_.clear();
  types_.clear();
  nextId_ = 1;
  roots.clear();
  barriers.reset();
  events.reset();
  metrics.reset();
  safepoints.configure(config.threads);
  collector = makeCollector(config.algo, *this);
  collector->setup();
  collector->attach();
}

void Runtime::reconfigure(const Config& cfg) {
  collector.reset();
  heap.clearRegions();
  config = cfg;
  heap = Heap(config.heapBase, config.heapBytes);
  reset();
}

bool Runtime::setAlgorithm(Algo a) {
  bool keepHeap = collector && usesMainRegion(config.algo) && usesMainRegion(a);
  config.algo = a;
  if (keepHeap) {
    collector = makeCollector(a, *this);
    collector->attach();
    return true;
  }
  reset();
  return false;
}

// ----------------------------------------------------------- object table
Addr Runtime::addrOf(ObjId id) const {
  auto it = table_.find(id);
  return it == table_.end() ? kNull : it->second;
}

uint16_t Runtime::typeId(const std::string& n) {
  for (size_t i = 0; i < types_.size(); ++i)
    if (types_[i] == n) return static_cast<uint16_t>(i);
  types_.push_back(n);
  return static_cast<uint16_t>(types_.size() - 1);
}

std::string Runtime::typeName(uint16_t t) const {
  return t < types_.size() ? types_[t] : std::string("?");
}

std::string Runtime::label(ObjId id) const {
  auto it = labels_.find(id);
  return it == labels_.end() ? std::string() : it->second;
}

std::string Runtime::name(ObjId id) const {
  std::string l = label(id);
  std::string num = "#" + std::to_string(id);
  return l.empty() ? "Object " + num : l + " (" + num + ")";
}

ObjId Runtime::resolve(const std::string& token) const {
  if (token.empty()) return 0;
  std::string t = token[0] == '#' ? token.substr(1) : token;
  bool digits = !t.empty();
  for (char c : t)
    if (!std::isdigit(static_cast<unsigned char>(c))) digits = false;
  if (digits) {
    if (t.size() > 9) return 0;  // cannot be a valid id (and would overflow)
    ObjId id = static_cast<ObjId>(std::stoul(t));
    return exists(id) ? id : 0;
  }
  for (const auto& kv : labels_)
    if (kv.second == token) return kv.first;
  return 0;
}

std::string Runtime::spaceName(Addr a) {
  Region* r = heap.regionOf(a);
  return r ? r->name : std::string("?");
}

void Runtime::forget(ObjId id) {
  table_.erase(id);
  labels_.erase(id);
}

void Runtime::beginPhase(const std::string& phase, const std::string& text) {
  events.setPhase(phase);
  if (!text.empty()) events.emit(EventType::PhaseStarted, 0, 0, 0, 0, 0, text);
}

bool Runtime::isYoungAddr(Addr a) {
  Region* r = heap.regionOf(a);
  return r && isYoung(r->kind);
}

bool Runtime::isOldAddr(Addr a) {
  Region* r = heap.regionOf(a);
  return r && r->kind == RegionKind::Old;
}

// -------------------------------------------------------------- mutator ops
Addr Runtime::tryAllocate(uint32_t size, uint32_t& actual) {
  actual = 0;
  return collector->allocate(size, actual);
}

std::string Runtime::freshLabel(ObjId id) const {
  for (char c = 'A'; c <= 'Z'; ++c) {
    std::string s(1, c);
    bool used = false;
    for (const auto& kv : labels_)
      if (kv.second == s) used = true;
    if (!used) return s;
  }
  return "N" + std::to_string(id);
}

ObjId Runtime::allocate(const std::string& type, uint32_t numRefs, uint32_t dataBytes,
                        const std::string& lbl) {
  if (numRefs > kMaxRefs) return 0;
  uint32_t size = objectSize(numRefs, dataBytes);
  uint32_t actual = 0;
  Addr a = tryAllocate(size, actual);
  if (a == kNull) {
    events.emit(EventType::AllocationFailed, 0, 0, 0, 0, size,
                "Allocation of " + std::to_string(size) + " bytes failed: no free block is large enough.");
    if (config.autoGC) {
      collect(CollectKind::Auto);
      a = tryAllocate(size, actual);
      if (a == kNull && config.algo == Algo::Generational) {
        collect(CollectKind::Major);
        a = tryAllocate(size, actual);
      }
    }
  }
  if (a == kNull) {
    events.emit(EventType::OutOfMemory, 0, 0, 0, 0, size,
                "OUT OF MEMORY: even after collecting there is no room for " + std::to_string(size) + " bytes.");
    return 0;
  }

  ObjId id = nextId_++;
  heap.mem().zero(a, actual);
  ObjectHeader* h = heap.mem().header(a);
  h->id = id;
  h->size = actual;
  h->type = typeId(type);
  h->numRefs = static_cast<uint8_t>(numRefs);
  h->color = kWhite;
  table_[id] = a;
  labels_[id] = lbl.empty() ? freshLabel(id) : lbl;

  events.emit(EventType::ObjectAllocated, id, 0, a, 0, actual,
              "Allocated " + name(id) + " [" + type + ", " + std::to_string(actual) + " bytes] at " +
                  hex(a) + " in " + spaceName(a) + ".");
  return id;
}

bool Runtime::setRef(ObjId src, uint32_t slot, ObjId dst) {
  Addr sa = addrOf(src);
  if (sa == kNull) return false;
  ObjectHeader* h = heap.mem().header(sa);
  if (slot >= h->numRefs) return false;
  Addr da = kNull;
  if (dst != 0) {
    da = addrOf(dst);
    if (da == kNull) return false;
  }
  Addr* field = heap.mem().refs(sa) + slot;
  Addr old = *field;
  *field = da;
  events.emit(EventType::ReferenceStored, src, dst, sa, da, slot,
              dst ? name(src) + ".refs[" + std::to_string(slot) + "] = " + name(dst)
                  : name(src) + ".refs[" + std::to_string(slot) + "] = null");
  collector->onReferenceStore(sa, old, da);
  return true;
}

bool Runtime::addRoot(const std::string& n, ObjId id, int thread) {
  Addr a = addrOf(id);
  if (a == kNull) return false;
  uint32_t rid = roots.add(n, thread >= 0 ? RootKind::Stack : RootKind::Global, thread, a);
  if (rid == 0) return false;
  events.emit(EventType::RootAdded, id, rid, a, 0, thread,
              std::string(thread >= 0 ? "Stack" : "Global") + " root '" + n + "' (root #" + std::to_string(rid) +
                  ") now references " + name(id) + ".");
  collector->onRootAdded(a);
  return true;
}

bool Runtime::removeRoot(const std::string& n) {
  Root r;
  if (!roots.remove(n, &r)) return false;
  ObjId id = r.addr ? heap.mem().header(r.addr)->id : 0;
  events.emit(EventType::RootRemoved, id, r.id, r.addr, 0, 0,
              "Root '" + n + "' (root #" + std::to_string(r.id) + ") removed. " +
                  (id ? name(id) + " is no longer referenced by it." : ""));
  collector->onRootRemoved(r.addr);
  return true;
}

void Runtime::collect(CollectKind kind) {
  bool stw = config.useSafepoints && safepoints.threadCount() > 0;
  if (stw) {
    safepoints.requestStop(events);
    safepoints.beginCollection(events);
  }
  try {
    collector->collect(kind);
  } catch (...) {
    if (stw) safepoints.resume(events);
    events.setPhase("");
    throw;
  }
  events.setPhase("");
  if (stw) safepoints.resume(events);
}

}  // namespace minigc
