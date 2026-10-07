// The five collectors. They all implement `Collector` and operate on the same
// runtime; the only thing they share is the heap, the roots and the event stream.
#pragma once
#include <deque>
#include <vector>

#include "minigc/collector.h"
#include "minigc/runtime.h"

namespace minigc {

// Reference counting, mark-sweep and mark-compact all use ONE free-list region.
class MainRegionCollector : public Collector {
 public:
  using Collector::Collector;
  void setup() override;
  Addr allocate(uint32_t size, uint32_t& actual) override;

 protected:
  Region& main();
  FreeListAllocator& freeList();
};

// Phase 5 -------------------------------------------------------------------
class RefCountCollector : public MainRegionCollector {
 public:
  using MainRegionCollector::MainRegionCollector;
  Algo algo() const override { return Algo::RefCount; }
  void attach() override;
  void collect(CollectKind kind) override;
  void onReferenceStore(Addr src, Addr oldDst, Addr newDst) override;
  void onRootAdded(Addr target) override { increment(target); }
  void onRootRemoved(Addr target) override { decrement(target); }

 private:
  void increment(Addr a);
  void decrement(Addr a);
  void reclaim(Addr a, const std::string& why);
};

// Phase 6 -------------------------------------------------------------------
class MarkSweepCollector : public MainRegionCollector {
 public:
  using MainRegionCollector::MainRegionCollector;
  Algo algo() const override { return Algo::MarkSweep; }
  void attach() override;
  void collect(CollectKind kind) override;
};

// Phase 7 -------------------------------------------------------------------
class MarkCompactCollector : public MainRegionCollector {
 public:
  using MainRegionCollector::MainRegionCollector;
  Algo algo() const override { return Algo::MarkCompact; }
  void attach() override;
  void collect(CollectKind kind) override;
};

// Phase 8 -------------------------------------------------------------------
class CopyingCollector : public Collector {
 public:
  using Collector::Collector;
  Algo algo() const override { return Algo::Copying; }
  void setup() override;
  Addr allocate(uint32_t size, uint32_t& actual) override;
  void collect(CollectKind kind) override;

 private:
  Addr evacuate(Addr a);
  Region* from_ = nullptr;
  Region* to_ = nullptr;
};

// Phases 9-10 ----------------------------------------------------------------
class GenerationalCollector : public Collector {
 public:
  using Collector::Collector;
  Algo algo() const override { return Algo::Generational; }
  void setup() override;
  Addr allocate(uint32_t size, uint32_t& actual) override;
  void collect(CollectKind kind) override;
  void onReferenceStore(Addr src, Addr oldDst, Addr newDst) override;

  Region* survivorInUse() const { return fromSurv_; }  // the survivor currently holding survivors

 private:
  void minor();
  void major();
  Addr evacuate(Addr a, Region* to, std::deque<Addr>* queue);
  bool inCollectionSet(Addr a) const;
  bool hasYoungReference(Addr obj);
  void swapSurvivors(Region* to);
  std::vector<Addr> rememberedHolders();

  Region *eden_ = nullptr, *survA_ = nullptr, *survB_ = nullptr, *old_ = nullptr;
  Region* fromSurv_ = nullptr;
  std::vector<Addr> promoted_;  // objects promoted during the current collection
};

}  // namespace minigc
