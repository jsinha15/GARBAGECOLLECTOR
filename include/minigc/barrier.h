// Write barrier + remembered set + simplified card table (Phase 10).
//
// A minor GC only traces the young generation. Old objects that point into it
// would be invisible roots, so every pointer store goes through a barrier that
// records old->young references.
#pragma once
#include <set>
#include <vector>

#include "minigc/common.h"
#include "minigc/events.h"
#include "minigc/heap.h"

namespace minigc {

class CardTable {
 public:
  void configure(Addr start, uint32_t size, uint32_t cardBytes);
  void clear();
  size_t cardCount() const { return dirty_.size(); }
  uint32_t cardBytes() const { return cardBytes_; }
  Addr start() const { return start_; }
  size_t indexOf(Addr a) const { return (a - start_) / cardBytes_; }
  bool dirty(size_t i) const { return i < dirty_.size() && dirty_[i]; }
  // Returns true if the card was clean before.
  bool markDirty(Addr a);
  void clean(size_t i) { if (i < dirty_.size()) dirty_[i] = 0; }
  std::vector<size_t> dirtyCards() const;

 private:
  Addr start_ = 0;
  uint32_t cardBytes_ = 128;
  std::vector<uint8_t> dirty_;
};

class BarrierManager {
 public:
  explicit BarrierManager(EventStream& ev) : events_(ev) {}

  void configure(Addr oldStart, uint32_t oldSize, uint32_t cardBytes) {
    cards_.configure(oldStart, oldSize, cardBytes);
    remembered_.clear();
  }
  void reset() { remembered_.clear(); cards_.clear(); }

  // The write barrier. Called on EVERY reference store while a generational
  // collector is active. Records the store iff it creates an old->young pointer.
  // Returns true when the store was recorded.
  bool onWrite(ObjId srcId, Addr srcAddr, bool srcIsOld, ObjId dstId, bool dstIsYoung,
               const std::string& srcName, const std::string& dstName);

  // Used by the collector to maintain the sets after a GC.
  bool addRemembered(ObjId id, Addr addr, const std::string& why);
  void removeRemembered(ObjId id);
  void clearAll();

  std::set<ObjId>& remembered() { return remembered_; }
  const std::set<ObjId>& remembered() const { return remembered_; }
  CardTable& cards() { return cards_; }
  const CardTable& cards() const { return cards_; }

 private:
  EventStream& events_;
  std::set<ObjId> remembered_;
  CardTable cards_;
};

}  // namespace minigc
