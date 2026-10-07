// The managed runtime: owns the heap, roots, barriers, safepoints and the
// currently selected collector, and exposes the "mutator" operations that a
// program running on top of it would perform (allocate, store a reference,
// register a root).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "minigc/barrier.h"
#include "minigc/common.h"
#include "minigc/config.h"
#include "minigc/events.h"
#include "minigc/heap.h"
#include "minigc/object.h"
#include "minigc/roots.h"
#include "minigc/safepoint.h"

namespace minigc {

class Collector;

class Runtime {
 public:
  explicit Runtime(const Config& cfg = Config());
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // ---- components (public on purpose: this is a learning lab, not an API)
  Config config;
  EventStream events;
  Metrics metrics;
  Heap heap;
  RootManager roots;
  BarrierManager barriers;
  SafepointManager safepoints;
  std::unique_ptr<Collector> collector;

  // ---- lifecycle
  void reset();                          // wipe heap/roots/events, keep config
  void reconfigure(const Config& cfg);   // new config, then reset
  // Switch collector. The heap is kept when both algorithms use the single
  // free-list layout (RC / Mark-Sweep / Mark-Compact); otherwise it is reset.
  // Returns true when the heap contents were preserved.
  bool setAlgorithm(Algo a);

  // ---- mutator operations
  // Returns the new object id, or 0 if the heap is exhausted.
  ObjId allocate(const std::string& type = "Node", uint32_t numRefs = 2, uint32_t dataBytes = 0,
                 const std::string& label = std::string());
  // src.refs[slot] = dst (dst == 0 stores null). Runs the collector's store hook
  // (reference counting / write barrier).
  bool setRef(ObjId src, uint32_t slot, ObjId dst);
  bool addRoot(const std::string& name, ObjId id, int thread = -1);
  bool removeRoot(const std::string& name);
  void collect(CollectKind kind = CollectKind::Auto);

  // ---- object table (id -> current address; kept up to date when objects move)
  bool exists(ObjId id) const { return table_.count(id) != 0; }
  Addr addrOf(ObjId id) const;
  ObjectHeader& header(ObjId id) { return *heap.mem().header(addrOf(id)); }
  const std::map<ObjId, Addr>& objects() const { return table_; }
  std::string typeName(uint16_t t) const;
  std::string label(ObjId id) const;
  std::string name(ObjId id) const;              // "A (#1)"
  ObjId resolve(const std::string& token) const; // "A", "#1" or "1" -> id (0 if unknown)
  std::string spaceName(Addr a);                 // region the address lives in

  // ---- helpers used by the collectors
  void relocate(ObjId id, Addr newAddr) { table_[id] = newAddr; }
  void forget(ObjId id);                          // the object is gone
  void beginPhase(const std::string& phase, const std::string& text);
  bool isYoungAddr(Addr a);
  bool isOldAddr(Addr a);
  uint16_t typeId(const std::string& name);

 private:
  Addr tryAllocate(uint32_t size, uint32_t& actual);
  std::string freshLabel(ObjId id) const;

  std::map<ObjId, Addr> table_;
  std::map<ObjId, std::string> labels_;
  std::vector<std::string> types_;
  ObjId nextId_ = 1;
};

}  // namespace minigc
