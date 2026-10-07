# Safepoints and stop-the-world

Source: `include/minigc/safepoint.h`, `src/core/safepoint.cpp`, `Runtime::collect`.

## 1. Concept
A collector that moves or frees objects needs the program (the *mutator*) to be **stopped** at a moment when its roots are well
defined. Threads do not stop anywhere: they stop at **safepoints** — places where they poll a flag and park. A **stop-the-world** GC
sets the flag, waits until every thread has parked, collects, then lets them go.

```
Thread 1 ───────────────● STOP
Thread 2 ───────────●   STOP
Thread 3 ─────────────────● STOP
                        ↓
                       GC
                        ↓
                    application resumes
```

## 2. Why it exists
If a thread kept running during compaction it could read a reference an instant before it is rewritten, or store a pointer into an
object that is being moved. Stopping the world makes the collector's view of the heap *atomic*.

## 3. Data structures
`SafepointManager`: simulated `SimThread`s (each with `ticksToPoll`, its distance to the next poll) and a world state
`RUNNING → GC_REQUESTED → SAFEPOINT → STOPPED → GC → RESUMED`. The threads are **simulated and deterministic** (thread *i* needs
`(2i mod 5) + 1` ticks): HeapLens teaches the concept, not an OS scheduler. Stack roots (`root add r A 1`) belong to a thread.

## 4. Algorithm
```
requestStop: state = GC_REQUESTED; emit SafepointRequested
             for tick = 1,2,3,…: each running thread whose ticksToPoll == tick polls, parks → ThreadReachedSafepoint
             when none is left: state = STOPPED; emit AllThreadsStopped
collect:     (the collector runs; nobody else touches the heap)
resume:      emit ThreadsResumed; all threads RUNNING
```
`Runtime::collect()` always brackets the collector with this protocol (disable with `set safepoints off`, change the thread count with
`threads N`). If the collector throws, the world is still resumed.

## 5. Example
`./build/minigc --demo 1 --trace` begins with `SafepointRequested`, three `ThreadReachedSafepoint` events (ticks 1, 3, 5), `AllThreadsStopped`,
then the GC events, and ends with `ThreadsResumed`.

## 6. Visualization
The strip under the timeline highlights the current world state; thread lanes turn red with a dot when parked.

## 7. Tests
`safepoint.manager_stops_every_thread_then_resumes`, `safepoint.threads_reach_the_safepoint_in_poll_order`,
`safepoint.gc_runs_only_between_stop_and_resume`, `safepoint.world_is_running_again_after_gc`,
`safepoint.can_be_disabled_and_thread_count_changed`, `safepoint.every_gc_algorithm_goes_through_the_protocol`.

## 8. How to run it
`threads 5`, `gc`, `events 30`.
