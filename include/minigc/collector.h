// The GarbageCollector interface. The runtime talks only to this abstraction,
// so any algorithm can drive the same heap. Algorithms communicate with the
// outside world exclusively by emitting events.
#pragma once
#include <deque>
#include <memory>
#include <vector>

#include "minigc/common.h"
#include "minigc/heap.h"

namespace minigc {

class Runtime;

class Collector {
 public:
  explicit Collector(Runtime& rt) : rt_(rt) {}
  virtual ~Collector() = default;

  virtual Algo algo() const = 0;
  // Lay out the heap regions this algorithm needs (called on reset).
  virtual void setup() = 0;
  // Called when this collector becomes the active one (also after setup()).
  virtual void attach() {}
  // Raw allocation policy: which region gets new objects.
  virtual Addr allocate(uint32_t size, uint32_t& actual) = 0;
  // Run one collection, stop-the-world. The runtime already stopped the threads.
  virtual void collect(CollectKind kind) = 0;

  // Mutator hooks. `src` is the holder, old/new the previous and new target.
  virtual void onReferenceStore(Addr src, Addr oldDst, Addr newDst) { (void)src; (void)oldDst; (void)newDst; }
  virtual void onRootAdded(Addr target) { (void)target; }
  virtual void onRootRemoved(Addr target) { (void)target; }

 protected:
  Runtime& rt_;
};

std::unique_ptr<Collector> makeCollector(Algo a, Runtime& rt);

// True if the algorithm uses the single free-list "Main" region layout.
inline bool usesMainRegion(Algo a) {
  return a == Algo::RefCount || a == Algo::MarkSweep || a == Algo::MarkCompact;
}

// ------------------------------------------------------------ shared tracing
// Tri-colour mark phase shared by Mark-Sweep, Mark-Compact and the major GC.
//   white = not reached yet (garbage if still white at the end)
//   gray  = discovered, waiting in the worklist
//   black = scanned: all its references have been followed
class Tracer {
 public:
  explicit Tracer(Runtime& rt) : rt_(rt) {}
  // Marks everything reachable from the roots, emitting events as it goes.
  // Afterwards reports every white object as unreachable.
  void markFromRoots();
  size_t marked() const { return marked_; }
  std::vector<ObjId> unreachable;  // ids of objects left white

 private:
  void shade(Addr a, const std::string& why);
  void scan(Addr a);
  Runtime& rt_;
  std::deque<Addr> work_;
  size_t marked_ = 0;
};

// Helpers for moving collectors.
// The address `target` should now be known by: its forwarding address if one
// was installed, otherwise unchanged.
Addr forwardedAddress(Runtime& rt, Addr target);
// Rewrite a root / a reference field to `newAddr` and emit ReferenceUpdated.
// The old target header must still be readable at its old address.
struct Root;
void updateRootRef(Runtime& rt, Root& root, Addr newAddr);
void updateFieldRef(Runtime& rt, Addr holder, uint32_t slot, Addr newAddr);

}  // namespace minigc
