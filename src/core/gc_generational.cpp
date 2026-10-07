// Phases 9 & 10 - Generational GC with write barrier, remembered set and card table.
//
//   +---------------------------- young generation ----------------------------+  +- old -+
//   | EDEN (new objects)          | SURVIVOR A       | SURVIVOR B               |  | OLD   |
//   +-----------------------------+------------------+--------------------------+  +-------+
//
// Weak generational hypothesis: most objects die young. So we collect only the
// young generation often (minor GC, cheap: copy the few survivors), and the
// whole heap rarely (major GC).
//
// Object life: Eden --survive--> Survivor --survive--> Survivor ... --age >=
// threshold--> Old. Two survivor spaces alternate: one holds the survivors of
// the last GC ("from"), the other is empty ("to") and receives the next ones.
#include <algorithm>
#include <stdexcept>

#include "minigc/gc_algorithms.h"

namespace minigc {

void GenerationalCollector::setup() {
  Heap& heap = rt_.heap;
  uint32_t unit = (heap.size() / 8) & ~7u;
  Addr a = heap.base();
  eden_ = &heap.addRegion(RegionKind::Eden, "EDEN", a, 3 * unit, true);
  a += 3 * unit;
  survA_ = &heap.addRegion(RegionKind::SurvivorA, "SURVIVOR A", a, unit, true);
  a += unit;
  survB_ = &heap.addRegion(RegionKind::SurvivorB, "SURVIVOR B", a, unit, true);
  a += unit;
  uint32_t oldSize = heap.end() - a;
  old_ = &heap.addRegion(RegionKind::Old, "OLD", a, oldSize, false);
  fromSurv_ = survB_;  // so the first minor GC copies into Survivor A
  rt_.barriers.configure(old_->start, oldSize, rt_.config.cardBytes);
}

Addr GenerationalCollector::allocate(uint32_t size, uint32_t& actual) {
  // Pretenuring: an object bigger than a survivor space could never be copied into one,
  // so it would be promoted at its first GC anyway. Allocate it in the old generation directly.
  if (size > survA_->size()) return old_->alloc->allocateBlock(size, actual);
  return eden_->alloc->allocateBlock(size, actual);
}

// -------------------------------------------------------------- write barrier
void GenerationalCollector::onReferenceStore(Addr src, Addr, Addr newDst) {
  if (newDst == kNull) return;
  Memory& mem = rt_.heap.mem();
  ObjId sid = mem.header(src)->id;
  ObjId did = mem.header(newDst)->id;
  rt_.barriers.onWrite(sid, src, rt_.isOldAddr(src), did, rt_.isYoungAddr(newDst), rt_.name(sid),
                       rt_.name(did));
}

// ------------------------------------------------------------------ helpers
bool GenerationalCollector::inCollectionSet(Addr a) const {
  Region* r = rt_.heap.regionOf(a);
  return r != nullptr && (r == eden_ || r == fromSurv_);
}

bool GenerationalCollector::hasYoungReference(Addr obj) {
  Memory& mem = rt_.heap.mem();
  ObjectHeader* h = mem.header(obj);
  for (uint32_t i = 0; i < h->numRefs; ++i) {
    Addr t = mem.refs(obj)[i];
    if (t != kNull && rt_.isYoungAddr(t)) return true;
  }
  return false;
}

std::vector<Addr> GenerationalCollector::rememberedHolders() {
  std::vector<Addr> out;
  if (!rt_.config.useCardTable) {
    for (ObjId id : rt_.barriers.remembered()) {
      Addr a = rt_.addrOf(id);
      if (a != kNull && rt_.isOldAddr(a)) out.push_back(a);
    }
  } else {
    const CardTable& cards = rt_.barriers.cards();
    rt_.heap.forEachObject(*old_, [&](Addr a, ObjectHeader&) {
      if (cards.dirty(cards.indexOf(a))) out.push_back(a);
    });
  }
  return out;
}

void GenerationalCollector::swapSurvivors(Region* to) {
  Region* oldFrom = fromSurv_;
  fromSurv_ = to;
  rt_.events.emit(EventType::SpacesSwapped, 0, 0, oldFrom->start, to->start, 0,
                  "SURVIVORS SWAPPED: " + to->name + " now holds the survivors; " + oldFrom->name +
                      " is empty and becomes the 'to' space of the next minor GC.");
}

Addr GenerationalCollector::evacuate(Addr a, Region* to, std::deque<Addr>* queue) {
  Runtime& rt = rt_;
  Memory& mem = rt.heap.mem();
  ObjectHeader* h = mem.header(a);
  if (h->forward) return h->forward;

  ObjId id = h->id;
  uint32_t size = h->size, actual = 0;
  uint32_t age = h->age;
  Region* src = rt.heap.regionOf(a);
  bool promote = (src != eden_) && age >= rt.config.promotionThreshold;
  std::string early;
  Addr n = kNull;
  if (!promote) {
    n = to->alloc->allocateBlock(size, actual);
    if (n == kNull) {
      promote = true;
      early = " (" + to->name + " is full, so it is promoted early)";
    }
  }
  if (promote) {
    n = old_->alloc->allocateBlock(size, actual);
    if (n == kNull) {
      rt.events.emit(EventType::OutOfMemory, id, 0, a, 0, size,
                     "PROMOTION FAILURE: the old generation has no room for " + rt.name(id) + ".");
      throw std::runtime_error("promotion failure: old generation is full");
    }
  }
  mem.move(n, a, size);
  if (actual > size) mem.zero(n + size, actual - size);
  ObjectHeader* nh = mem.header(n);
  nh->size = actual;
  nh->forward = 0;
  nh->color = kWhite;
  nh->age = static_cast<uint8_t>(promote ? age : age + 1);
  h->forward = n;
  rt.relocate(id, n);
  std::string from = src ? src->name : "?";

  rt.events.emit(EventType::ObjectMoved, id, 0, a, n, size,
                 rt.name(id) + " is live: copied from " + hex(a) + " (" + from + ") to " + hex(n) + " (" +
                     rt.spaceName(n) + ").");
  if (promote) {
    promoted_.push_back(n);
    rt.events.emit(EventType::ObjectPromoted, id, 0, a, n, nh->age,
                   rt.name(id) + " survived and is PROMOTED to the old generation" +
                       (early.empty() ? " because its age (" + std::to_string(age) +
                                            ") reached the promotion threshold (" +
                                            std::to_string(rt.config.promotionThreshold) + ")."
                                      : early + ".") );
  } else {
    rt.events.emit(EventType::ObjectAged, id, 0, a, n, nh->age,
                   rt.name(id) + " survived a minor GC: age " + std::to_string(age) + " -> " +
                       std::to_string(nh->age) + ", now in " + to->name + ".");
  }
  if (queue) queue->push_back(n);
  return n;
}

// ------------------------------------------------------------------ dispatch
void GenerationalCollector::collect(CollectKind kind) {
  if (kind == CollectKind::Major) major();
  else minor();
}

// ------------------------------------------------------------------ minor GC
void GenerationalCollector::minor() {
  Runtime& rt = rt_;
  Memory& mem = rt.heap.mem();
  Region* to = (fromSurv_ == survA_) ? survB_ : survA_;

  auto used = [](Region* r) { return r->alloc->walkLimit() - r->start; };
  uint32_t youngUsed = used(eden_) + used(fromSurv_);
  // Worst case (everything is live): objects old enough are promoted, and whatever does not
  // fit into the empty survivor space is promoted early.
  uint32_t agedBytes = 0;
  rt.heap.forEachObject(*fromSurv_, [&](Addr, ObjectHeader& h) {
    if (h.age >= rt.config.promotionThreshold) agedBytes += h.size;
  });
  uint32_t rest = youngUsed - agedBytes;
  uint32_t mustPromote = agedBytes + (rest > to->size() ? rest - to->size() : 0);
  if (rt.heap.region(RegionKind::Old)->alloc->freeBytes() < mustPromote) {
    rt.beginPhase("ESCALATE",
                  "PROMOTION GUARANTEE FAILED: the old generation may not have room for the survivors, so a "
                  "MAJOR GC runs instead.");
    major();
    return;
  }

  promoted_.clear();
  rt.beginPhase("GC START", "");
  rt.events.emit(EventType::MinorGCStarted, 0, 0, 0, 0, 0,
                 "MINOR GC started (stop-the-world). Only the YOUNG generation (Eden + " + fromSurv_->name +
                     ") is collected; old objects are not traced. Old->young pointers are found through the " +
                     (rt.config.useCardTable ? "dirty cards." : "remembered set."));
  to->alloc->reset();
  std::deque<Addr> queue;
  std::vector<Addr> holders = rememberedHolders();

  // 1. roots ---------------------------------------------------------------
  rt.beginPhase("ROOT SCAN", "ROOT SCAN: roots that point into the young generation keep those objects alive.");
  for (Root& root : rt.roots.all()) {
    if (root.addr == kNull) continue;
    ObjId id = mem.header(root.addr)->id;
    if (!inCollectionSet(root.addr)) {
      rt.events.emit(EventType::RootDiscovered, id, root.id, root.addr, 0, 0,
                     "Root '" + root.name + "' points at " + rt.name(id) + " in the " + rt.spaceName(root.addr) +
                         " - not part of this minor GC, so it is left alone.");
      continue;
    }
    rt.events.emit(EventType::RootDiscovered, id, root.id, root.addr, 0, 0,
                   rt.name(id) + " is reachable because it is directly referenced by GC root '" + root.name + "'.");
    Addr n = evacuate(root.addr, to, &queue);
    updateRootRef(rt, root, n);
  }

  // 2. old->young pointers --------------------------------------------------
  rt.beginPhase("REMEMBERED SET SCAN",
                "REMEMBERED SET SCAN: old objects recorded by the write barrier are extra roots. Their young "
                "targets must survive even though no root reaches them directly.");
  for (Addr holder : holders) {
    ObjectHeader* hh = mem.header(holder);
    rt.events.emit(EventType::ObjectScanned, hh->id, 0, holder, 0, hh->numRefs,
                   "Scanning old object " + rt.name(hh->id) + " from the remembered set: its young targets are live.");
    for (uint32_t i = 0; i < hh->numRefs; ++i) {
      Addr t = mem.refs(holder)[i];
      if (t == kNull || !inCollectionSet(t)) continue;
      rt.events.emit(EventType::ReferenceTraversed, hh->id, mem.header(t)->id, holder, t, i,
                     rt.name(hh->id) + " (Old) --> " + rt.name(mem.header(t)->id) + " (Young)");
      Addr n = evacuate(t, to, &queue);
      updateFieldRef(rt, holder, i, n);
    }
  }

  // 3. transitive closure over the copied objects -----------------------------
  rt.beginPhase("COPY SURVIVORS", "COPY SURVIVORS: follow references out of every object copied so far.");
  while (!queue.empty()) {
    Addr a = queue.front();
    queue.pop_front();
    ObjectHeader* h = mem.header(a);
    rt.events.emit(EventType::ObjectScanned, h->id, 0, a, 0, h->numRefs,
                   "Scanning " + rt.name(h->id) + " (now in " + rt.spaceName(a) + ").");
    for (uint32_t i = 0; i < h->numRefs; ++i) {
      Addr t = mem.refs(a)[i];
      if (t == kNull || !inCollectionSet(t)) continue;
      rt.events.emit(EventType::ReferenceTraversed, h->id, mem.header(t)->id, a, t, i,
                     rt.name(h->id) + " --> " + rt.name(mem.header(t)->id));
      Addr n = evacuate(t, to, &queue);
      updateFieldRef(rt, a, i, n);
    }
  }

  // 4. whatever was not forwarded is garbage -----------------------------------
  rt.beginPhase("RECLAIM YOUNG",
                "RECLAIM YOUNG: objects in Eden/" + fromSurv_->name +
                    " without a forwarding pointer were not reachable from any root or remembered old object. "
                    "They are dropped by resetting the whole space.");
  int64_t reclaimed = 0, bytes = 0;
  for (Region* r : {eden_, fromSurv_}) {
    rt.heap.forEachObject(*r, [&](Addr a, ObjectHeader& h) {
      if (h.forward != 0) return;
      rt.events.emit(EventType::ObjectReclaimed, h.id, 0, a, 0, h.size,
                     rt.name(h.id) + " reclaimed: it was not copied because no root and no old object reaches it. "
                                     "(It died young.)");
      ++reclaimed;
      bytes += h.size;
      rt.forget(h.id);
    });
    mem.zero(r->start, r->size());
    r->alloc->reset();
  }
  swapSurvivors(to);

  // 5. maintain remembered set + cards ------------------------------------------
  rt.beginPhase("REMEMBERED SET UPDATE",
                "REMEMBERED SET UPDATE: promoted objects may still point at young objects (they must be remembered); "
                "old objects that no longer point into the young generation can be forgotten.");
  std::vector<ObjId> before(rt.barriers.remembered().begin(), rt.barriers.remembered().end());
  for (ObjId id : before) {
    Addr a = rt.addrOf(id);
    if (a == kNull || !hasYoungReference(a)) rt.barriers.removeRemembered(id);
  }
  for (Addr a : promoted_) {
    if (hasYoungReference(a)) {
      ObjId id = mem.header(a)->id;
      rt.barriers.addRemembered(id, a, rt.name(id) + " was promoted but still points into the young generation, so it is remembered.");
    }
  }
  size_t dirtyBefore = rt.barriers.cards().dirtyCards().size();
  rt.barriers.cards().clear();
  for (ObjId id : rt.barriers.remembered()) rt.barriers.cards().markDirty(rt.addrOf(id));
  if (dirtyBefore > 0)
    rt.events.emit(EventType::CardCleaned, 0, 0, 0, 0, static_cast<int64_t>(dirtyBefore),
                   "Card table cleaned; cards of objects that still point into the young generation stay dirty.");

  rt.events.emit(EventType::MinorGCCompleted, 0, 0, 0, 0, reclaimed,
                 "MINOR GC complete: " + std::to_string(reclaimed) + " young object(s) reclaimed (" +
                     std::to_string(bytes) + " bytes), " + std::to_string(promoted_.size()) + " promoted. Eden is empty.");
}

// ------------------------------------------------------------------ major GC
void GenerationalCollector::major() {
  Runtime& rt = rt_;
  Memory& mem = rt.heap.mem();
  Region* to = (fromSurv_ == survA_) ? survB_ : survA_;

  promoted_.clear();
  rt.beginPhase("GC START", "");
  rt.events.emit(EventType::MajorGCStarted, 0, 0, 0, 0, 0,
                 "MAJOR (FULL) GC started (stop-the-world). The whole heap - young AND old - is traced from the roots.");

  Tracer tracer(rt);
  tracer.markFromRoots();

  // 1. sweep the old generation -------------------------------------------------
  rt.beginPhase("SWEEP OLD", "SWEEP OLD: unmarked old objects are garbage; their blocks return to the old free list.");
  struct Dead { Addr addr; ObjId id; uint32_t size; };
  std::vector<Dead> dead;
  rt.heap.forEachObject(*old_, [&](Addr a, ObjectHeader& h) {
    if (h.color == kWhite) dead.push_back({a, h.id, h.size});
    else h.color = kWhite;
  });
  int64_t reclaimed = 0, bytes = 0;
  for (const Dead& d : dead) {
    rt.events.emit(EventType::ObjectReclaimed, d.id, 0, d.addr, 0, d.size,
                   rt.name(d.id) + " reclaimed: it was UNREACHABLE from every GC root.");
    ++reclaimed;
    bytes += d.size;
    rt.forget(d.id);
    mem.writeFreeHeader(d.addr, d.size);
  }
  static_cast<FreeListAllocator&>(*old_->alloc).rebuildFromHeaders();

  // 2. move live young objects --------------------------------------------------
  rt.beginPhase("EVACUATE YOUNG",
                "EVACUATE YOUNG: live objects in Eden/" + fromSurv_->name + " move to " + to->name +
                    " (or to the old generation if they are old enough); dead ones are dropped.");
  to->alloc->reset();
  std::vector<Addr> young;
  for (Region* r : {eden_, fromSurv_})
    rt.heap.forEachObject(*r, [&](Addr a, ObjectHeader&) { young.push_back(a); });
  for (Addr a : young) {
    ObjectHeader* h = mem.header(a);
    if (h->color == kWhite) {
      rt.events.emit(EventType::ObjectReclaimed, h->id, 0, a, 0, h->size,
                     rt.name(h->id) + " reclaimed: it was UNREACHABLE from every GC root.");
      ++reclaimed;
      bytes += h->size;
      rt.forget(h->id);
    } else {
      evacuate(a, to, nullptr);
    }
  }

  // 3. fix references ---------------------------------------------------------------
  rt.beginPhase("UPDATE REFERENCES",
                "UPDATE REFERENCES: young objects moved, so every root and every reference field that pointed at "
                "an old copy is rewritten to the forwarding address.");
  for (Root& root : rt.roots.all()) {
    if (root.addr == kNull) continue;
    Addr n = forwardedAddress(rt, root.addr);
    if (n != root.addr) updateRootRef(rt, root, n);
  }
  for (Region* r : {old_, to}) {
    rt.heap.forEachObject(*r, [&](Addr a, ObjectHeader& h) {
      for (uint32_t i = 0; i < h.numRefs; ++i) {
        Addr t = mem.refs(a)[i];
        if (t == kNull) continue;
        Addr n = forwardedAddress(rt, t);
        if (n != t) updateFieldRef(rt, a, i, n);
      }
    });
  }

  // 4. reset the young spaces ---------------------------------------------------------
  for (Region* r : {eden_, fromSurv_}) {
    mem.zero(r->start, r->size());
    r->alloc->reset();
  }
  swapSurvivors(to);

  // 5. rebuild the remembered set from scratch --------------------------------------------
  rt.beginPhase("REMEMBERED SET REBUILD", "REMEMBERED SET REBUILD: after a full GC the old->young pointers are recomputed from the live old objects.");
  rt.barriers.clearAll();
  std::vector<Addr> oldObjs;
  rt.heap.forEachObject(*old_, [&](Addr a, ObjectHeader&) { oldObjs.push_back(a); });
  for (Addr a : oldObjs) {
    if (!hasYoungReference(a)) continue;
    ObjId id = mem.header(a)->id;
    rt.barriers.cards().markDirty(a);
    rt.barriers.addRemembered(id, a, rt.name(id) + " still points into the young generation, so it is remembered.");
  }

  rt.events.emit(EventType::MajorGCCompleted, 0, 0, 0, 0, reclaimed,
                 "MAJOR GC complete: " + std::to_string(reclaimed) + " object(s) reclaimed (" + std::to_string(bytes) +
                     " bytes), " + std::to_string(promoted_.size()) + " promoted.");
}

}  // namespace minigc
