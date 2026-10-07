// Basic safepoints and stop-the-world coordination (Phase 11).
//
// Real runtimes stop mutator threads at "safepoints": places in the code where
// the thread's roots are known precisely. Here the application threads are
// SIMULATED and deterministic: each thread needs a fixed number of "ticks" of
// work before it next polls the safepoint flag.
#pragma once
#include <string>
#include <vector>

#include "minigc/common.h"
#include "minigc/events.h"

namespace minigc {

enum class ThreadState { Running, AtSafepoint };
enum class WorldState { Running, GcRequested, Safepoint, Stopped, Collecting, Resumed };

const char* worldStateName(WorldState s);

struct SimThread {
  int index = 0;
  ThreadState state = ThreadState::Running;
  uint32_t ticksToPoll = 1;  // mutator ticks until this thread reaches its next safepoint poll
};

class SafepointManager {
 public:
  void configure(uint32_t threads);

  // RUNNING -> GC_REQUESTED -> (each thread polls) -> SAFEPOINT -> STOPPED
  void requestStop(EventStream& ev);
  // STOPPED -> GC (the collector is running)
  void beginCollection(EventStream& ev);
  // GC -> RESUMED -> RUNNING
  void resume(EventStream& ev);

  WorldState state() const { return state_; }
  bool allStopped() const;
  const std::vector<SimThread>& threads() const { return threads_; }
  uint32_t threadCount() const { return static_cast<uint32_t>(threads_.size()); }

 private:
  std::vector<SimThread> threads_;
  WorldState state_ = WorldState::Running;
};

}  // namespace minigc
