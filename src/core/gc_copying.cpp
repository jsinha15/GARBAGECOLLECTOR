// Phase 8 - Copying GC (Cheney's semispace algorithm).
// The heap is two equal spaces. New objects are bump-allocated in FROM-SPACE.
// A collection copies every reachable object into TO-SPACE, leaving a
// forwarding pointer in the old copy so that other references can be fixed up.
// Then the spaces swap roles. Garbage is never touched - it is simply left
// behind - and the survivors end up contiguous, so there is no fragmentation.
#include <utility>

#include "minigc/gc_algorithms.h"

namespace minigc {

void CopyingCollector::setup() {
  uint32_t half = (rt_.heap.size() / 2) & ~7u;
  from_ = &rt_.heap.addRegion(RegionKind::FromSpace, "FROM-SPACE", rt_.heap.base(), half, true);
  to_ = &rt_.heap.addRegion(RegionKind::ToSpace, "TO-SPACE", rt_.heap.base() + half, rt_.heap.size() - half, true);
}

Addr CopyingCollector::allocate(uint32_t size, uint32_t& actual) {
  return from_->alloc->allocateBlock(size, actual);
}

Addr CopyingCollector::evacuate(Addr a) {
  Memory& mem = rt_.heap.mem();
  ObjectHeader* h = mem.header(a);
  if (h->forward) return h->forward;  // already copied: reuse the forwarding pointer
  ObjId id = h->id;
  uint32_t size = h->size, actual = 0;
  Addr n = to_->alloc->allocateBlock(size, actual);
  mem.move(n, a, size);
  ObjectHeader* nh = mem.header(n);
  nh->forward = 0;
  nh->color = kWhite;
  h->forward = n;  // forwarding pointer left in from-space
  rt_.relocate(id, n);
  rt_.events.emit(EventType::ObjectMoved, id, 0, a, n, size,
                  rt_.name(id) + " is live: copied from " + hex(a) + " (from-space) to " + hex(n) +
                      " (to-space). A forwarding pointer " + hex(a) + " -> " + hex(n) +
                      " is left in the old copy.");
  return n;
}

void CopyingCollector::collect(CollectKind) {
  Runtime& rt = rt_;
  Memory& mem = rt.heap.mem();

  rt.beginPhase("GC START", "");
  rt.events.emit(EventType::GCStarted, 0, 0, 0, 0, 0,
                 "COPYING GC started (stop-the-world). Live objects will be copied from FROM-SPACE into the "
                 "empty TO-SPACE.");
  to_->alloc->reset();

  rt.beginPhase("ROOT EVACUATION",
                "ROOT EVACUATION: copy every object directly referenced by a root into TO-SPACE and repoint the root.");
  for (Root& root : rt.roots.all()) {
    if (root.addr == kNull) continue;
    ObjId id = mem.header(root.addr)->id;
    rt.events.emit(EventType::RootDiscovered, id, root.id, root.addr, 0, 0,
                   rt.name(id) + " is reachable because it is directly referenced by GC root '" + root.name + "'.");
    Addr n = evacuate(root.addr);
    updateRootRef(rt, root, n);
  }

  rt.beginPhase("SCAN TO-SPACE",
                "SCAN TO-SPACE (Cheney): walk the copied objects in order; copy whatever they reference. "
                "The scan pointer chases the allocation pointer until they meet - then everything live is copied.");
  BumpAllocator& bump = static_cast<BumpAllocator&>(*to_->alloc);
  Addr scan = to_->start;
  while (scan < bump.cursor()) {
    ObjectHeader* h = mem.header(scan);
    uint32_t size = h->size;
    rt.events.emit(EventType::ObjectScanned, h->id, 0, scan, 0, h->numRefs,
                   "Scanning " + rt.name(h->id) + " in TO-SPACE: every reference it holds still points into FROM-SPACE.");
    for (uint32_t i = 0; i < h->numRefs; ++i) {
      Addr t = mem.refs(scan)[i];
      if (t == kNull) continue;
      ObjectHeader* th = mem.header(t);
      rt.events.emit(EventType::ReferenceTraversed, h->id, th->id, scan, t, i,
                     rt.name(h->id) + " --> " + rt.name(th->id));
      Addr n = evacuate(t);
      updateFieldRef(rt, scan, i, n);
    }
    scan += size;
  }

  rt.beginPhase("DISCARD FROM-SPACE",
                "DISCARD FROM-SPACE: anything in the old space without a forwarding pointer was never reached, "
                "so it is garbage. We do not even look at it individually - the whole space is reused.");
  int64_t reclaimed = 0, bytes = 0;
  rt.heap.forEachObject(*from_, [&](Addr a, ObjectHeader& h) {
    if (h.forward == 0) {
      rt.events.emit(EventType::ObjectReclaimed, h.id, 0, a, 0, h.size,
                     rt.name(h.id) + " was not copied: no GC root could reach it, so it dies with the old space.");
      ++reclaimed;
      bytes += h.size;
      rt.forget(h.id);
    }
  });
  mem.zero(from_->start, from_->size());
  from_->alloc->reset();

  std::swap(from_, to_);
  from_->kind = RegionKind::FromSpace;
  from_->name = "FROM-SPACE";
  to_->kind = RegionKind::ToSpace;
  to_->name = "TO-SPACE";
  rt.events.emit(EventType::SpacesSwapped, 0, 0, from_->start, to_->start, 0,
                 "SPACES SWAPPED: the old to-space (now holding only live, contiguous objects) becomes the new "
                 "FROM-SPACE where allocation continues. The old from-space is empty and will be the next TO-SPACE.");
  rt.events.emit(EventType::GCCompleted, 0, 0, 0, 0, reclaimed,
                 "COPYING GC complete: " + std::to_string(reclaimed) + " object(s) discarded (" + std::to_string(bytes) +
                     " bytes). Survivors are packed together: free space is one contiguous block.");
}

}  // namespace minigc
