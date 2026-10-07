#include "minigc/safepoint.h"

namespace minigc {

const char* worldStateName(WorldState s) {
  switch (s) {
    case WorldState::Running: return "RUNNING";
    case WorldState::GcRequested: return "GC_REQUESTED";
    case WorldState::Safepoint: return "SAFEPOINT";
    case WorldState::Stopped: return "STOPPED";
    case WorldState::Collecting: return "GC";
    case WorldState::Resumed: return "RESUMED";
  }
  return "?";
}

void SafepointManager::configure(uint32_t n) {
  threads_.clear();
  for (uint32_t i = 0; i < n; ++i) {
    SimThread t;
    t.index = static_cast<int>(i);
    t.ticksToPoll = (i * 2) % 5 + 1;  // 1, 3, 5, 2, 4, 1, ... deterministic "distance to next poll"
    threads_.push_back(t);
  }
  state_ = WorldState::Running;
}

bool SafepointManager::allStopped() const {
  for (const auto& t : threads_)
    if (t.state != ThreadState::AtSafepoint) return false;
  return true;
}

void SafepointManager::requestStop(EventStream& ev) {
  state_ = WorldState::GcRequested;
  ev.emit(EventType::SafepointRequested, 0, 0, 0, 0, static_cast<int64_t>(threads_.size()),
          "GC REQUESTED: the collector raises the safepoint flag. Application threads keep running "
          "until they next poll it; the GC cannot start while any thread might still be changing the heap.");
  uint32_t tick = 0;
  uint32_t remaining = static_cast<uint32_t>(threads_.size());
  while (remaining > 0) {
    ++tick;
    for (auto& t : threads_) {
      if (t.state != ThreadState::Running || t.ticksToPoll != tick) continue;
      t.state = ThreadState::AtSafepoint;
      --remaining;
      ev.emit(EventType::ThreadReachedSafepoint, static_cast<ObjId>(t.index), 0, 0, 0, tick,
              "Thread " + std::to_string(t.index) + " polled the safepoint flag after " +
                  std::to_string(tick) + " tick(s) and parked: its stack roots are now stable.");
    }
  }
  state_ = WorldState::Safepoint;
  state_ = WorldState::Stopped;
  ev.emit(EventType::AllThreadsStopped, 0, 0, 0, 0, tick,
          "STOPPED: every application thread is parked at a safepoint. The world is stopped, so the "
          "heap cannot change underneath the collector.");
}

void SafepointManager::beginCollection(EventStream&) { state_ = WorldState::Collecting; }

void SafepointManager::resume(EventStream& ev) {
  state_ = WorldState::Resumed;
  for (auto& t : threads_) t.state = ThreadState::Running;
  ev.emit(EventType::ThreadsResumed, 0, 0, 0, 0, static_cast<int64_t>(threads_.size()),
          "RESUMED: the collector finished and released the safepoint. Application threads continue.");
  state_ = WorldState::Running;
}

}  // namespace minigc
