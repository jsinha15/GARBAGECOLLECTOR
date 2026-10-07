// The GC event stream: the ONLY channel between the runtime and everything that
// wants to watch it (logger, metrics, visualization). GC algorithms never touch
// the UI; they just emit structured events.
#pragma once
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "minigc/common.h"

namespace minigc {

enum class EventType {
  // collection lifecycle
  GCStarted, GCCompleted, MinorGCStarted, MinorGCCompleted, MajorGCStarted, MajorGCCompleted,
  PhaseStarted,
  // tracing
  RootDiscovered, ObjectDiscovered, ObjectMarked, ObjectScanned, ReferenceTraversed,
  MarkingCompleted, ObjectUnreachable,
  // reclamation
  ObjectReclaimed, FreeListRebuilt,
  // movement
  ForwardingAssigned, ObjectMoved, ReferenceUpdated, SpacesSwapped,
  // generational
  ObjectAged, ObjectPromoted, WriteBarrierTriggered, RememberedSetAdded, RememberedSetRemoved,
  CardDirtied, CardCleaned,
  // reference counting
  RefCountChanged, CycleDetected,
  // mutator
  ObjectAllocated, ReferenceStored, RootAdded, RootRemoved, AllocationFailed, OutOfMemory,
  // safepoints
  SafepointRequested, ThreadReachedSafepoint, AllThreadsStopped, ThreadsResumed
};

const char* eventName(EventType t);

// One structured fact about what the runtime just did. `text` is the plain
// language explanation shown in the UI ("what happened and WHY").
struct Event {
  uint64_t seq = 0;
  EventType type = EventType::PhaseStarted;
  ObjId id = 0;      // primary object (or thread index for safepoint events)
  ObjId id2 = 0;     // secondary object / root number / slot, see docs/visualization.md
  Addr addr = 0;     // old address / relevant address
  Addr addr2 = 0;    // new address
  int64_t value = 0; // size, count, age, ... depending on type
  std::string phase; // GC phase active when the event was emitted
  std::string text;  // human readable explanation
};

class EventStream {
 public:
  using Listener = std::function<void(const Event&)>;

  void subscribe(Listener l) { listeners_.push_back(std::move(l)); }
  void emit(EventType t, ObjId id = 0, ObjId id2 = 0, Addr addr = 0, Addr addr2 = 0,
            int64_t value = 0, const std::string& text = std::string());

  void setPhase(const std::string& p) { phase_ = p; }
  const std::string& phase() const { return phase_; }

  const std::vector<Event>& history() const { return history_; }
  size_t size() const { return history_.size(); }
  void clearHistory() { history_.clear(); }
  // Forget history, phase and the sequence counter: a reset runtime replays identically.
  void reset() { history_.clear(); phase_.clear(); nextSeq_ = 1; ++epoch_; }
  uint32_t epoch() const { return epoch_; }  // bumps on every reset()

 private:
  std::vector<Listener> listeners_;
  std::vector<Event> history_;
  std::string phase_;
  uint64_t nextSeq_ = 1;
  uint32_t epoch_ = 0;
};

// ---------------------------------------------------------------- logging
enum class LogLevel { Off = 0, Error, Warn, Info, Debug, Trace };

class Logger {
 public:
  static Logger& get();
  void setLevel(LogLevel l) { level_ = l; }
  LogLevel level() const { return level_; }
  void setSink(std::ostream* s) { sink_ = s; }
  void log(LogLevel l, const std::string& msg);

 private:
  LogLevel level_ = LogLevel::Off;
  std::ostream* sink_ = nullptr;  // null => std::cerr
};

// ---------------------------------------------------------------- metrics
struct GCStats {  // statistics of the most recent collection
  int64_t marked = 0, reclaimedObjects = 0, reclaimedBytes = 0;
  int64_t moved = 0, promoted = 0, referencesUpdated = 0;
};

struct Metrics {
  int64_t allocations = 0, allocatedBytes = 0;
  int64_t collections = 0, minorCollections = 0, majorCollections = 0;
  int64_t reclaimedObjects = 0, reclaimedBytes = 0;
  int64_t objectsMoved = 0, objectsPromoted = 0, referencesUpdated = 0;
  int64_t barrierHits = 0, rememberedSetAdds = 0;
  int64_t safepointStops = 0;
  GCStats last;

  void attach(EventStream& s);  // subscribe to the stream
  void reset() { *this = Metrics(); }
  void onEvent(const Event& e);
  std::map<std::string, int64_t> toMap() const;
};

}  // namespace minigc
