#include "minigc/barrier.h"

#include <algorithm>

namespace minigc {

void CardTable::configure(Addr start, uint32_t size, uint32_t cardBytes) {
  start_ = start;
  cardBytes_ = cardBytes == 0 ? 128 : cardBytes;
  dirty_.assign((size + cardBytes_ - 1) / cardBytes_, 0);
}

void CardTable::clear() { std::fill(dirty_.begin(), dirty_.end(), 0); }

bool CardTable::markDirty(Addr a) {
  size_t i = indexOf(a);
  if (i >= dirty_.size()) return false;
  bool wasClean = dirty_[i] == 0;
  dirty_[i] = 1;
  return wasClean;
}

std::vector<size_t> CardTable::dirtyCards() const {
  std::vector<size_t> out;
  for (size_t i = 0; i < dirty_.size(); ++i)
    if (dirty_[i]) out.push_back(i);
  return out;
}

bool BarrierManager::onWrite(ObjId srcId, Addr srcAddr, bool srcIsOld, ObjId dstId, bool dstIsYoung,
                             const std::string& srcName, const std::string& dstName) {
  if (!srcIsOld || !dstIsYoung) return false;  // not an old->young store: nothing to remember
  events_.emit(EventType::WriteBarrierTriggered, srcId, dstId, srcAddr, 0, 0,
               "WRITE BARRIER: " + srcName + " (Old) now points to " + dstName +
                   " (Young). A minor GC does not trace the old generation, so without "
                   "this record " + dstName + " could be freed while still in use.");
  if (cards_.markDirty(srcAddr)) {
    events_.emit(EventType::CardDirtied, srcId, 0, srcAddr, 0,
                 static_cast<int64_t>(cards_.indexOf(srcAddr)),
                 "Card #" + std::to_string(cards_.indexOf(srcAddr)) + " (covering " + hex(srcAddr) +
                     ") marked DIRTY.");
  }
  addRemembered(srcId, srcAddr, srcName + " added to the REMEMBERED SET: it holds a pointer into the young generation.");
  return true;
}

bool BarrierManager::addRemembered(ObjId id, Addr addr, const std::string& why) {
  bool inserted = remembered_.insert(id).second;
  if (inserted) events_.emit(EventType::RememberedSetAdded, id, 0, addr, 0, 0, why);
  return inserted;
}

void BarrierManager::removeRemembered(ObjId id) {
  if (remembered_.erase(id))
    events_.emit(EventType::RememberedSetRemoved, id, 0, 0, 0, 0,
                 "Object #" + std::to_string(id) + " removed from the remembered set: it no longer points into the young generation.");
}

void BarrierManager::clearAll() {
  remembered_.clear();
  cards_.clear();
}

}  // namespace minigc
