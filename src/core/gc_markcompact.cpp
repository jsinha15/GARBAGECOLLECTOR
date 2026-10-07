// Phase 7 - Mark-Compact (a "sliding", Lisp-2 style compactor).
//   1. MARK                 find the live objects
//   2. COMPUTE ADDRESSES    walk the heap; live objects get a new, packed address,
//                           stored in the header's forwarding field
//   3. UPDATE REFERENCES    every root and every reference field is rewritten
//                           to the target's forwarding address
//   4. MOVE                 slide the objects down to their new addresses
// Objects keep their relative order, and free space becomes one block at the end.
#include "minigc/gc_algorithms.h"

namespace minigc {

void MarkCompactCollector::attach() {
  rt_.heap.forEachObject(main(), [](Addr, ObjectHeader& h) { h.color = kWhite; h.forward = 0; });
}

void MarkCompactCollector::collect(CollectKind) {
  Runtime& rt = rt_;
  Memory& mem = rt.heap.mem();
  Region& r = main();

  rt.beginPhase("GC START", "");
  rt.events.emit(EventType::GCStarted, 0, 0, 0, 0, 0,
                 "MARK-COMPACT GC started (stop-the-world). Live objects will be slid together so the free "
                 "space becomes one contiguous block.");

  Tracer tracer(rt);
  tracer.markFromRoots();

  // --- dead objects simply disappear: compaction overwrites their space
  rt.beginPhase("RECLAIM", "RECLAIM: unreachable objects are discarded; their space will be overwritten by compaction.");
  struct Dead { Addr addr; ObjId id; uint32_t size; };
  std::vector<Dead> dead;
  rt.heap.forEachObject(r, [&](Addr a, ObjectHeader& h) {
    if (h.color == kWhite) dead.push_back({a, h.id, h.size});
  });
  int64_t bytes = 0;
  for (const Dead& d : dead) {
    rt.events.emit(EventType::ObjectReclaimed, d.id, 0, d.addr, 0, d.size,
                   rt.name(d.id) + " reclaimed: no GC root could reach it.");
    bytes += d.size;
    rt.forget(d.id);
    mem.writeFreeHeader(d.addr, d.size);
  }

  // --- phase 2: forwarding addresses
  rt.beginPhase("COMPUTE ADDRESSES",
                "COMPUTE ADDRESSES: walk the heap in address order and assign each live object the next free "
                "address from the start of the heap. The new address is stored in the object's header (forwarding information).");
  Addr dest = r.start;
  uint32_t moves = 0;
  rt.heap.forEachObject(r, [&](Addr a, ObjectHeader& h) {
    h.forward = dest;
    bool moves_ = dest != a;
    if (moves_) ++moves;
    rt.events.emit(EventType::ForwardingAssigned, h.id, 0, a, dest, h.size,
                   moves_ ? rt.name(h.id) + ": old address " + hex(a) + ", new address " + hex(dest) + " (will move)."
                          : rt.name(h.id) + " stays at " + hex(a) + " (already in its final position).");
    dest += h.size;
  });
  Addr liveEnd = dest;

  // --- phase 3: update references (before anything moves, headers are still at old addresses)
  rt.beginPhase("UPDATE REFERENCES",
                "UPDATE REFERENCES: objects are about to move, so every reference to them must be rewritten "
                "first. Each root and each reference field takes its target's forwarding address.");
  for (Root& root : rt.roots.all()) {
    if (root.addr == kNull) continue;
    Addr n = forwardedAddress(rt, root.addr);
    if (n != root.addr) updateRootRef(rt, root, n);
  }
  rt.heap.forEachObject(r, [&](Addr a, ObjectHeader& h) {
    for (uint32_t i = 0; i < h.numRefs; ++i) {
      Addr t = mem.refs(a)[i];
      if (t == kNull) continue;
      Addr n = forwardedAddress(rt, t);
      if (n != t) updateFieldRef(rt, a, i, n);
    }
  });

  // --- phase 4: slide objects down
  rt.beginPhase("MOVE OBJECTS", "MOVE OBJECTS: slide each live object to its forwarding address, lowest address first.");
  rt.heap.forEachObject(r, [&](Addr a, ObjectHeader& h) {
    ObjId id = h.id;
    uint32_t size = h.size;
    Addr to = h.forward;
    if (to != a) {
      mem.move(to, a, size);
      rt.relocate(id, to);
      rt.events.emit(EventType::ObjectMoved, id, 0, a, to, size,
                     rt.name(id) + " moved: " + hex(a) + " -> " + hex(to) + ".");
    }
    ObjectHeader* nh = mem.header(to);
    nh->forward = 0;
    nh->color = kWhite;
  });

  freeList().resetFreeTail(liveEnd);
  rt.beginPhase("FREE SPACE", "");
  rt.events.emit(EventType::FreeListRebuilt, 0, 0, liveEnd, r.end, r.end - liveEnd,
                 "Heap compacted: live objects occupy [" + hex(r.start) + ", " + hex(liveEnd) +
                     ") and ONE free block of " + std::to_string(r.end - liveEnd) + " bytes starts at " +
                     hex(liveEnd) + ". Fragmentation is gone.");
  rt.events.emit(EventType::GCCompleted, 0, 0, 0, 0, static_cast<int64_t>(dead.size()),
                 "MARK-COMPACT complete: " + std::to_string(dead.size()) + " object(s) reclaimed (" +
                     std::to_string(bytes) + " bytes), " + std::to_string(moves) + " object(s) moved.");
}

}  // namespace minigc
