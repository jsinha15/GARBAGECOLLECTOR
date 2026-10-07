// Phase 6 - Mark-Sweep.
//   MARK : trace from the roots, colouring reachable objects.
//   SWEEP: walk the whole heap linearly; every object that is still white is
//          garbage and is turned into a free block; then rebuild the free list.
#include "minigc/gc_algorithms.h"

namespace minigc {

void MarkSweepCollector::attach() {
  // Make sure no stale colours survive a switch from another algorithm.
  rt_.heap.forEachObject(main(), [](Addr, ObjectHeader& h) { h.color = kWhite; h.forward = 0; });
}

void MarkSweepCollector::collect(CollectKind) {
  Runtime& rt = rt_;
  rt.beginPhase("GC START", "");
  rt.events.emit(EventType::GCStarted, 0, 0, 0, 0, 0,
                 "MARK-SWEEP GC started. The world is stopped (stop-the-world), so the object graph cannot "
                 "change while we trace it.");

  Tracer tracer(rt);
  tracer.markFromRoots();

  rt.beginPhase("SWEEP",
                "SWEEP: walk every block in the heap. Objects that are still WHITE were never reached "
                "from a root, so their memory is returned to the free list.");
  struct Dead { Addr addr; ObjId id; uint32_t size; };
  std::vector<Dead> dead;
  uint32_t liveObjects = 0;
  rt.heap.forEachObject(main(), [&](Addr a, ObjectHeader& h) {
    if (h.color == kWhite) {
      dead.push_back({a, h.id, h.size});
    } else {
      h.color = kWhite;  // reset for the next cycle
      ++liveObjects;
    }
  });
  int64_t bytes = 0;
  for (const Dead& d : dead) {
    rt.events.emit(EventType::ObjectReclaimed, d.id, 0, d.addr, 0, d.size,
                   rt.name(d.id) + " reclaimed: it was UNREACHABLE (no GC root could reach it). "
                                   "It was not removed because it was old - age is irrelevant to Mark-Sweep.");
    bytes += d.size;
    rt.forget(d.id);
    rt.heap.mem().writeFreeHeader(d.addr, d.size);
  }
  freeList().rebuildFromHeaders();

  rt.beginPhase("FREE LIST", "");
  rt.events.emit(EventType::FreeListRebuilt, 0, 0, 0, 0, freeList().freeBlockCount(),
                 "Free list rebuilt: " + std::to_string(freeList().freeBlockCount()) +
                     " free block(s), adjacent free blocks coalesced. Note that live objects did NOT move, "
                     "so free space may be fragmented.");
  rt.events.emit(EventType::GCCompleted, 0, 0, 0, 0, static_cast<int64_t>(dead.size()),
                 "MARK-SWEEP complete: " + std::to_string(liveObjects) + " object(s) survive, " +
                     std::to_string(dead.size()) + " reclaimed (" + std::to_string(bytes) + " bytes).");
}

}  // namespace minigc
