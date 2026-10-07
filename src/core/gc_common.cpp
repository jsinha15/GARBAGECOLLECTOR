// Code shared by the collectors: the tri-colour tracer, reference rewriting
// helpers, and the collector factory.
#include <algorithm>

#include "minigc/collector.h"
#include "minigc/gc_algorithms.h"

namespace minigc {

// ------------------------------------------------------------------ Tracer
void Tracer::shade(Addr a, const std::string& why) {
  ObjectHeader* h = rt_.heap.mem().header(a);
  h->color = kGray;
  work_.push_back(a);
  ++marked_;
  rt_.events.emit(EventType::ObjectDiscovered, h->id, 0, a, 0, 0, why);
  rt_.events.emit(EventType::ObjectMarked, h->id, 0, a, 0, 0,
                  rt_.name(h->id) + " is marked (GRAY): it is queued and its references will be followed.");
}

void Tracer::scan(Addr a) {
  Memory& mem = rt_.heap.mem();
  ObjectHeader* h = mem.header(a);
  ObjId id = h->id;
  rt_.events.emit(EventType::ObjectScanned, id, 0, a, 0, h->numRefs,
                  "Scanning " + rt_.name(id) + ": following its " + std::to_string(h->numRefs) +
                      " reference slot(s). When done it turns BLACK (scanned).");
  h->color = kBlack;
  Addr* refs = mem.refs(a);
  for (uint32_t i = 0; i < h->numRefs; ++i) {
    Addr t = refs[i];
    if (t == kNull) continue;
    ObjectHeader* th = mem.header(t);
    bool fresh = th->color == kWhite;
    rt_.events.emit(EventType::ReferenceTraversed, id, th->id, a, t, i,
                    rt_.name(id) + " --> " + rt_.name(th->id) + (fresh ? "" : " (already marked, nothing to do)"));
    if (fresh)
      shade(t, rt_.name(th->id) + " is reachable because " + rt_.name(id) + " references it.");
  }
}

void Tracer::markFromRoots() {
  Memory& mem = rt_.heap.mem();
  rt_.beginPhase("ROOT SCAN",
                 "ROOT SCAN: tracing starts from the GC roots (globals and thread stacks). "
                 "Whatever a root points at is reachable.");
  for (Root& r : rt_.roots.all()) {
    if (r.addr == kNull) continue;
    ObjectHeader* h = mem.header(r.addr);
    rt_.events.emit(EventType::RootDiscovered, h->id, r.id, r.addr, 0, 0,
                    rt_.name(h->id) + " is reachable because it is directly referenced by GC root '" + r.name +
                        "' (root #" + std::to_string(r.id) + ").");
    if (h->color == kWhite) shade(r.addr, rt_.name(h->id) + " is discovered through root '" + r.name + "'.");
  }

  rt_.beginPhase("MARK", "MARK: repeatedly take a GRAY object, scan its references, and turn it BLACK.");
  while (!work_.empty()) {
    Addr a = work_.front();
    work_.pop_front();
    scan(a);
  }
  rt_.events.emit(EventType::MarkingCompleted, 0, 0, 0, 0, static_cast<int64_t>(marked_),
                  "MARKING COMPLETE: " + std::to_string(marked_) +
                      " object(s) are LIVE (reachable). Every object still WHITE is unreachable garbage.");

  unreachable.clear();
  std::vector<std::pair<ObjId, Addr>> dead;
  for (auto& region : rt_.heap.regions())
    rt_.heap.forEachObject(*region, [&](Addr a, ObjectHeader& h) {
      if (h.color == kWhite) dead.push_back({h.id, a});
    });
  std::sort(dead.begin(), dead.end());
  for (auto& d : dead) {
    unreachable.push_back(d.first);
    rt_.events.emit(EventType::ObjectUnreachable, d.first, 0, d.second, 0, 0,
                    rt_.name(d.first) + " is UNREACHABLE: no path exists from any GC root to it.");
  }
}

// ------------------------------------------------------- reference rewriting
Addr forwardedAddress(Runtime& rt, Addr target) {
  if (target == kNull) return kNull;
  uint32_t f = rt.heap.mem().header(target)->forward;
  return f ? f : target;
}

void updateRootRef(Runtime& rt, Root& root, Addr newAddr) {
  Addr old = root.addr;
  ObjId tid = rt.heap.mem().header(old)->id;
  root.addr = newAddr;
  rt.events.emit(EventType::ReferenceUpdated, 0, tid, old, newAddr, root.id,
                 "Root '" + root.name + "' (root #" + std::to_string(root.id) + ") pointed at " + rt.name(tid) +
                     " at " + hex(old) + ". The object moved, so the root is rewritten: old reference " +
                     hex(old) + " -> new reference " + hex(newAddr) + ".");
}

void updateFieldRef(Runtime& rt, Addr holder, uint32_t slot, Addr newAddr) {
  Memory& mem = rt.heap.mem();
  Addr* field = mem.refs(holder) + slot;
  Addr old = *field;
  ObjId tid = mem.header(old)->id;
  ObjId hid = mem.header(holder)->id;
  *field = newAddr;
  rt.events.emit(EventType::ReferenceUpdated, hid, tid, old, newAddr, slot,
                 rt.name(hid) + ".refs[" + std::to_string(slot) + "] pointed at " + rt.name(tid) + " at " +
                     hex(old) + ". The target moved: old reference " + hex(old) + " -> new reference " +
                     hex(newAddr) + ".");
}

// ------------------------------------------------------ main-region layout
void MainRegionCollector::setup() {
  rt_.heap.addRegion(RegionKind::Main, "HEAP", rt_.heap.base(), rt_.heap.size(), false);
}

Region& MainRegionCollector::main() { return *rt_.heap.region(RegionKind::Main); }

FreeListAllocator& MainRegionCollector::freeList() {
  return static_cast<FreeListAllocator&>(*main().alloc);
}

Addr MainRegionCollector::allocate(uint32_t size, uint32_t& actual) {
  return main().alloc->allocateBlock(size, actual);
}

// ----------------------------------------------------------------- factory
std::unique_ptr<Collector> makeCollector(Algo a, Runtime& rt) {
  switch (a) {
    case Algo::RefCount: return std::make_unique<RefCountCollector>(rt);
    case Algo::MarkSweep: return std::make_unique<MarkSweepCollector>(rt);
    case Algo::MarkCompact: return std::make_unique<MarkCompactCollector>(rt);
    case Algo::Copying: return std::make_unique<CopyingCollector>(rt);
    case Algo::Generational: return std::make_unique<GenerationalCollector>(rt);
  }
  return nullptr;
}

}  // namespace minigc
