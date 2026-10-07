// Phase 5 - Reference Counting.
// Every object carries a count of the references that point at it (heap fields
// + roots). The count is maintained eagerly on every store; when it reaches 0
// the object is freed immediately and its children are decremented in turn.
// There is no tracing, so reference cycles are never reclaimed: the collect()
// pass below only *diagnoses* them.
#include "minigc/gc_algorithms.h"
#include "minigc/graph.h"

namespace minigc {

void RefCountCollector::attach() {
  // Recompute counts from scratch (needed when switching from a tracing GC).
  Memory& mem = rt_.heap.mem();
  rt_.heap.forEachObject(main(), [](Addr, ObjectHeader& h) { h.refCount = 0; h.color = kWhite; h.forward = 0; });
  rt_.heap.forEachObject(main(), [&](Addr a, ObjectHeader& h) {
    for (uint32_t i = 0; i < h.numRefs; ++i)
      if (Addr t = mem.refs(a)[i]) ++mem.header(t)->refCount;
  });
  for (const Root& r : rt_.roots.all())
    if (r.addr) ++mem.header(r.addr)->refCount;
}

void RefCountCollector::increment(Addr a) {
  if (a == kNull) return;
  ObjectHeader* h = rt_.heap.mem().header(a);
  uint32_t before = h->refCount++;
  rt_.events.emit(EventType::RefCountChanged, h->id, 0, a, 0, h->refCount,
                  rt_.name(h->id) + " refCount " + std::to_string(before) + " -> " + std::to_string(h->refCount) +
                      " (a new reference was created).");
}

void RefCountCollector::decrement(Addr a) {
  if (a == kNull) return;
  ObjectHeader* h = rt_.heap.mem().header(a);
  if (h->refCount == 0) return;
  uint32_t before = h->refCount--;
  rt_.events.emit(EventType::RefCountChanged, h->id, 0, a, 0, h->refCount,
                  rt_.name(h->id) + " refCount " + std::to_string(before) + " -> " + std::to_string(h->refCount) +
                      " (a reference was removed).");
  if (h->refCount == 0)
    reclaim(a, rt_.name(h->id) + " reclaimed: its reference count reached 0, so nothing points at it any more.");
}

void RefCountCollector::reclaim(Addr a, const std::string& why) {
  Memory& mem = rt_.heap.mem();
  ObjectHeader* h = mem.header(a);
  ObjId id = h->id;
  uint32_t size = h->size;
  std::vector<Addr> children;  // copy: the block is about to be recycled
  for (uint32_t i = 0; i < h->numRefs; ++i)
    if (mem.refs(a)[i]) children.push_back(mem.refs(a)[i]);

  rt_.events.emit(EventType::ObjectReclaimed, id, 0, a, 0, size, why);
  rt_.forget(id);
  freeList().release(a, size);

  // Recursive reclamation: dropping this object drops its outgoing references.
  for (Addr c : children) decrement(c);
}

void RefCountCollector::onReferenceStore(Addr, Addr oldDst, Addr newDst) {
  // Increment first so that overwriting a slot with the same target never hits zero.
  increment(newDst);
  decrement(oldDst);
}

void RefCountCollector::collect(CollectKind) {
  Runtime& rt = rt_;
  rt.beginPhase("GC START", "");
  rt.events.emit(EventType::GCStarted, 0, 0, 0, 0, 0,
                 "REFERENCE COUNTING does not trace. Garbage was already reclaimed eagerly the moment a count "
                 "hit 0. This pass frees never-referenced objects and diagnoses leaks.");

  // 1. objects that nothing ever referenced (count 0)
  rt.beginPhase("ZERO-COUNT SWEEP", "ZERO-COUNT SWEEP: free objects whose reference count is 0.");
  std::vector<Addr> zero;
  rt.heap.forEachObject(main(), [&](Addr a, ObjectHeader& h) {
    if (h.refCount == 0) zero.push_back(a);
  });
  int64_t before = rt.metrics.reclaimedObjects;
  for (Addr a : zero) {
    ObjectHeader* h = rt.heap.mem().header(a);
    if (h->id == 0 || h->refCount != 0) continue;  // already freed by a recursive reclaim
    reclaim(a, rt.name(h->id) + " reclaimed: refCount is 0 (nothing ever referenced it).");
  }

  // 2. cycle diagnosis (read-only)
  rt.beginPhase("CYCLE DIAGNOSIS",
                "CYCLE DIAGNOSIS: ask a tracing question - is any object still counted but unreachable from the roots?");
  ObjectGraph graph(rt);
  auto cycles = graph.unreachableCycles();
  std::vector<ObjId> leaked = graph.unreachable();
  for (ObjId id : leaked) {
    Addr a = rt.addrOf(id);
    rt.events.emit(EventType::ObjectUnreachable, id, 0, a, 0, rt.heap.mem().header(a)->refCount,
                   rt.name(id) + " is UNREACHABLE but its refCount is " +
                       std::to_string(rt.heap.mem().header(a)->refCount) + " - it is leaked.");
  }
  for (const auto& cycle : cycles) {
    std::string names;
    for (ObjId id : cycle) names += (names.empty() ? "" : ", ") + rt.name(id);
    rt.events.emit(EventType::CycleDetected, cycle[0], 0, 0, 0, static_cast<int64_t>(cycle.size()),
                   "UNREACHABLE CYCLE: " + names +
                       ". Reference counting cannot reclaim this cycle because each object still has an "
                       "incoming reference from another unreachable object, so no count ever reaches 0.");
  }
  rt.events.emit(EventType::GCCompleted, 0, 0, 0, 0, rt.metrics.reclaimedObjects - before,
                 "Reference-counting pass complete: " + std::to_string(rt.metrics.reclaimedObjects - before) +
                     " object(s) freed, " + std::to_string(leaked.size()) + " leaked object(s) in " +
                     std::to_string(cycles.size()) + " cycle(s). A tracing GC (Mark-Sweep) would reclaim them.");
}

}  // namespace minigc
