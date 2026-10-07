#include "minigc/events.h"

#include <iostream>

namespace minigc {

const char* eventName(EventType t) {
  switch (t) {
    case EventType::GCStarted: return "GCStarted";
    case EventType::GCCompleted: return "GCCompleted";
    case EventType::MinorGCStarted: return "MinorGCStarted";
    case EventType::MinorGCCompleted: return "MinorGCCompleted";
    case EventType::MajorGCStarted: return "MajorGCStarted";
    case EventType::MajorGCCompleted: return "MajorGCCompleted";
    case EventType::PhaseStarted: return "PhaseStarted";
    case EventType::RootDiscovered: return "RootDiscovered";
    case EventType::ObjectDiscovered: return "ObjectDiscovered";
    case EventType::ObjectMarked: return "ObjectMarked";
    case EventType::ObjectScanned: return "ObjectScanned";
    case EventType::ReferenceTraversed: return "ReferenceTraversed";
    case EventType::MarkingCompleted: return "MarkingCompleted";
    case EventType::ObjectUnreachable: return "ObjectUnreachable";
    case EventType::ObjectReclaimed: return "ObjectReclaimed";
    case EventType::FreeListRebuilt: return "FreeListRebuilt";
    case EventType::ForwardingAssigned: return "ForwardingAssigned";
    case EventType::ObjectMoved: return "ObjectMoved";
    case EventType::ReferenceUpdated: return "ReferenceUpdated";
    case EventType::SpacesSwapped: return "SpacesSwapped";
    case EventType::ObjectAged: return "ObjectAged";
    case EventType::ObjectPromoted: return "ObjectPromoted";
    case EventType::WriteBarrierTriggered: return "WriteBarrierTriggered";
    case EventType::RememberedSetAdded: return "RememberedSetAdded";
    case EventType::RememberedSetRemoved: return "RememberedSetRemoved";
    case EventType::CardDirtied: return "CardDirtied";
    case EventType::CardCleaned: return "CardCleaned";
    case EventType::RefCountChanged: return "RefCountChanged";
    case EventType::CycleDetected: return "CycleDetected";
    case EventType::ObjectAllocated: return "ObjectAllocated";
    case EventType::ReferenceStored: return "ReferenceStored";
    case EventType::RootAdded: return "RootAdded";
    case EventType::RootRemoved: return "RootRemoved";
    case EventType::AllocationFailed: return "AllocationFailed";
    case EventType::OutOfMemory: return "OutOfMemory";
    case EventType::SafepointRequested: return "SafepointRequested";
    case EventType::ThreadReachedSafepoint: return "ThreadReachedSafepoint";
    case EventType::AllThreadsStopped: return "AllThreadsStopped";
    case EventType::ThreadsResumed: return "ThreadsResumed";
  }
  return "?";
}

void EventStream::emit(EventType t, ObjId id, ObjId id2, Addr addr, Addr addr2, int64_t value,
                       const std::string& text) {
  Event e;
  e.seq = nextSeq_++;
  e.type = t;
  e.id = id;
  e.id2 = id2;
  e.addr = addr;
  e.addr2 = addr2;
  e.value = value;
  e.phase = phase_;
  e.text = text;
  history_.push_back(e);
  for (auto& l : listeners_) l(history_.back());
}

// ---------------------------------------------------------------- logging
Logger& Logger::get() {
  static Logger instance;
  return instance;
}

void Logger::log(LogLevel l, const std::string& msg) {
  if (level_ == LogLevel::Off || l > level_) return;
  static const char* names[] = {"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
  std::ostream& os = sink_ ? *sink_ : std::cerr;
  os << "[minigc " << names[static_cast<int>(l)] << "] " << msg << "\n";
}

// ---------------------------------------------------------------- metrics
void Metrics::attach(EventStream& s) {
  s.subscribe([this](const Event& e) { onEvent(e); });
}

void Metrics::onEvent(const Event& e) {
  switch (e.type) {
    case EventType::GCStarted:
    case EventType::MinorGCStarted:
    case EventType::MajorGCStarted:
      ++collections;
      if (e.type == EventType::MinorGCStarted) ++minorCollections;
      if (e.type == EventType::MajorGCStarted) ++majorCollections;
      last = GCStats();
      break;
    case EventType::ObjectAllocated:
      ++allocations;
      allocatedBytes += e.value;
      break;
    case EventType::ObjectMarked: ++last.marked; break;
    case EventType::ObjectReclaimed:
      ++reclaimedObjects;
      reclaimedBytes += e.value;
      ++last.reclaimedObjects;
      last.reclaimedBytes += e.value;
      break;
    case EventType::ObjectMoved: ++objectsMoved; ++last.moved; break;
    case EventType::ObjectPromoted: ++objectsPromoted; ++last.promoted; break;
    case EventType::ReferenceUpdated: ++referencesUpdated; ++last.referencesUpdated; break;
    case EventType::WriteBarrierTriggered: ++barrierHits; break;
    case EventType::RememberedSetAdded: ++rememberedSetAdds; break;
    case EventType::AllThreadsStopped: ++safepointStops; break;
    default: break;
  }
  Logger::get().log(LogLevel::Debug, std::string(eventName(e.type)) + ": " + e.text);
}

std::map<std::string, int64_t> Metrics::toMap() const {
  return {{"allocations", allocations},
          {"allocatedBytes", allocatedBytes},
          {"collections", collections},
          {"minorCollections", minorCollections},
          {"majorCollections", majorCollections},
          {"reclaimedObjects", reclaimedObjects},
          {"reclaimedBytes", reclaimedBytes},
          {"objectsMoved", objectsMoved},
          {"objectsPromoted", objectsPromoted},
          {"referencesUpdated", referencesUpdated},
          {"barrierHits", barrierHits},
          {"rememberedSetAdds", rememberedSetAdds},
          {"safepointStops", safepointStops},
          {"lastMarked", last.marked},
          {"lastReclaimedObjects", last.reclaimedObjects},
          {"lastReclaimedBytes", last.reclaimedBytes},
          {"lastMoved", last.moved},
          {"lastPromoted", last.promoted}};
}

}  // namespace minigc
