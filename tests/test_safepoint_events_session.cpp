// Phase 11 safepoints, the event stream / visualization model, and the command session.
#include <sstream>

#include "minigc/visualization.h"
#include "test_util.h"

using namespace tu;

// ------------------------------------------------------------- safepoints
TEST("safepoint.manager_stops_every_thread_then_resumes") {
  SafepointManager sp;
  EventStream ev;
  sp.configure(4);
  CHECK_EQ(sp.threadCount(), 4u);
  CHECK(!sp.allStopped());
  CHECK(sp.state() == WorldState::Running);
  sp.requestStop(ev);
  CHECK(sp.allStopped());
  CHECK(sp.state() == WorldState::Stopped);
  sp.beginCollection(ev);
  CHECK(sp.state() == WorldState::Collecting);
  sp.resume(ev);
  CHECK(sp.state() == WorldState::Running);
  CHECK(!sp.allStopped());
}

TEST("safepoint.threads_reach_the_safepoint_in_poll_order") {
  SafepointManager sp;
  EventStream ev;
  sp.configure(3);                        // poll distances 1, 3, 5
  sp.requestStop(ev);
  std::vector<ObjId> order;
  std::vector<int64_t> ticks;
  for (const Event& e : ev.history())
    if (e.type == EventType::ThreadReachedSafepoint) { order.push_back(e.id); ticks.push_back(e.value); }
  CHECK((order == std::vector<ObjId>{0, 1, 2}));
  CHECK((ticks == std::vector<int64_t>{1, 3, 5}));
  CHECK_EQ(ev.history().front().type == EventType::SafepointRequested, true);
  CHECK_EQ(ev.history().back().type == EventType::AllThreadsStopped, true);
}

TEST("safepoint.gc_runs_only_between_stop_and_resume") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  node(rt, "A");
  rt.collect();
  size_t req = 0, stopped = 0, started = 0, completed = 0, resumed = 0, i = 0;
  for (const Event& e : rt.events.history()) {
    ++i;
    if (e.type == EventType::SafepointRequested) req = i;
    if (e.type == EventType::AllThreadsStopped) stopped = i;
    if (e.type == EventType::GCStarted) started = i;
    if (e.type == EventType::GCCompleted) completed = i;
    if (e.type == EventType::ThreadsResumed) resumed = i;
  }
  CHECK(req > 0 && req < stopped);
  CHECK(stopped < started);
  CHECK(started < completed);
  CHECK(completed < resumed);
  CHECK_EQ(count(rt, EventType::ThreadReachedSafepoint), 3u);
}

TEST("safepoint.world_is_running_again_after_gc") {
  Runtime rt(cfgFor(Algo::Copying));
  node(rt, "A");
  rt.collect();
  CHECK(rt.safepoints.state() == WorldState::Running);
  for (const SimThread& t : rt.safepoints.threads()) CHECK(t.state == ThreadState::Running);
}

TEST("safepoint.can_be_disabled_and_thread_count_changed") {
  Config c = cfgFor(Algo::MarkSweep);
  c.useSafepoints = false;
  Runtime rt(c);
  rt.collect();
  CHECK_EQ(count(rt, EventType::SafepointRequested), 0u);
  Session s;
  s.run("threads 5");
  s.run("gc");
  CHECK_EQ(count(s.runtime(), EventType::ThreadReachedSafepoint), 5u);
}

TEST("safepoint.every_gc_algorithm_goes_through_the_protocol") {
  for (Algo a : {Algo::RefCount, Algo::MarkSweep, Algo::MarkCompact, Algo::Copying, Algo::Generational}) {
    Runtime rt(cfgFor(a));
    node(rt, "A");
    rt.collect();
    CHECK_EQ(count(rt, EventType::AllThreadsStopped), 1u);
    CHECK_EQ(count(rt, EventType::ThreadsResumed), 1u);
  }
}

// ---------------------------------------------------------- event stream
static std::string scenarioEvents(Algo a) {
  Session s(cfgFor(a));
  for (const char* line : {"new Node 2 0 A", "new Node 2 0 B", "new Node 2 0 C", "new Node 2 0 D", "root add r A",
                           "ref A 0 B", "ref C 0 D", "gc", "gc", "new Node 1 0 E", "ref B 1 E", "gc major"})
    s.run(line);
  return eventsToJson(s.runtime().events.history(), 0, s.runtime().events.size());
}

TEST("events.same_scenario_gives_identical_event_streams") {
  for (Algo a : {Algo::RefCount, Algo::MarkSweep, Algo::MarkCompact, Algo::Copying, Algo::Generational})
    CHECK(scenarioEvents(a) == scenarioEvents(a));
}

TEST("events.reset_replays_identically") {
  Session s;
  s.run("demo 1");
  s.run("gc");
  std::string a = eventsToJson(s.runtime().events.history(), 0, s.runtime().events.size());
  s.run("reset");
  s.run("demo 1");
  s.run("gc");
  std::string b = eventsToJson(s.runtime().events.history(), 0, s.runtime().events.size());
  CHECK(a == b);
}

TEST("events.sequence_numbers_increase_by_one") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  buildRandomGraph(rt, 3, 10);
  rt.collect();
  uint64_t prev = 0;
  for (const Event& e : rt.events.history()) {
    CHECK_EQ(e.seq, prev + 1);
    prev = e.seq;
  }
}

TEST("events.listeners_receive_every_event") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  size_t seen = 0;
  rt.events.subscribe([&](const Event&) { ++seen; });
  node(rt, "A");
  rt.collect();
  CHECK_EQ(seen, rt.events.size());
  CHECK(seen > 5);
}

TEST("events.every_event_explains_itself") {
  for (Algo a : {Algo::RefCount, Algo::MarkSweep, Algo::MarkCompact, Algo::Copying, Algo::Generational}) {
    Runtime rt(cfgFor(a));
    buildRandomGraph(rt, 11, 16);
    rt.collect(a == Algo::Generational ? CollectKind::Minor : CollectKind::Auto);
    if (a == Algo::Generational) rt.collect(CollectKind::Major);
    for (const Event& e : rt.events.history()) {
      if (e.text.empty()) testfw::fail(__FILE__, __LINE__, std::string("event without explanation: ") + eventName(e.type));
    }
  }
}

TEST("events.phase_is_stamped_on_gc_events") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  node(rt, "A");
  rt.collect();
  bool sawMark = false, sawSweep = false;
  for (const Event& e : rt.events.history()) {
    if (e.phase == "MARK") sawMark = true;
    if (e.phase == "SWEEP") sawSweep = true;
  }
  CHECK(sawMark && sawSweep);
}

TEST("events.metrics_count_what_happened") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  for (const char* l : {"A", "x", "B"}) node(rt, l);
  rt.addRoot("rA", rt.resolve("A"));
  rt.addRoot("rB", rt.resolve("B"));
  rt.collect();
  CHECK_EQ(rt.metrics.allocations, 3);
  CHECK_EQ(rt.metrics.allocatedBytes, 96);
  CHECK_EQ(rt.metrics.collections, 1);
  CHECK_EQ(rt.metrics.reclaimedObjects, 1);
  CHECK_EQ(rt.metrics.objectsMoved, 1);        // only B slides down
  CHECK_EQ(rt.metrics.safepointStops, 1);
  CHECK_EQ(rt.metrics.toMap()["lastMoved"], 1);
}

TEST("events.logger_receives_debug_messages") {
  std::ostringstream sink;
  Logger::get().setSink(&sink);
  Logger::get().setLevel(LogLevel::Debug);
  Runtime rt(cfgFor(Algo::MarkSweep));
  node(rt, "A");
  Logger::get().setLevel(LogLevel::Off);
  Logger::get().setSink(nullptr);
  CHECK(sink.str().find("ObjectAllocated") != std::string::npos);
}

// --------------------------------------------------- visualization model
static bool balanced(const std::string& j) {
  int brace = 0, bracket = 0;
  bool inStr = false;
  for (size_t i = 0; i < j.size(); ++i) {
    char c = j[i];
    if (inStr) {
      if (c == '\\') ++i;
      else if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') inStr = true;
    else if (c == '{') ++brace;
    else if (c == '}') --brace;
    else if (c == '[') ++bracket;
    else if (c == ']') --bracket;
    if (brace < 0 || bracket < 0) return false;
  }
  return brace == 0 && bracket == 0 && !inStr;
}

TEST("events.snapshot_json_is_well_formed_for_every_algorithm") {
  for (Algo a : {Algo::RefCount, Algo::MarkSweep, Algo::MarkCompact, Algo::Copying, Algo::Generational}) {
    Runtime rt(cfgFor(a));
    buildRandomGraph(rt, 5, 16);
    std::string j = snapshotJson(rt);
    CHECK(balanced(j));
    CHECK(j.find("\"regions\"") != std::string::npos);
    CHECK(j.find("\"objects\"") != std::string::npos);
    CHECK(j.find("\"roots\"") != std::string::npos);
    CHECK(j.find("\"safepoint\"") != std::string::npos);
    rt.collect();
    CHECK(balanced(snapshotJson(rt)));
    CHECK(balanced(eventsToJson(rt.events.history(), 0, rt.events.size())));
  }
}

TEST("events.snapshot_explains_why_each_object_is_live_or_garbage") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (const char* l : {"A", "B", "C"}) node(rt, l);
  rt.addRoot("r", rt.resolve("A"));
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  std::string j = snapshotJson(rt);
  CHECK(j.find("Root 'r' -> A (#1) -> B (#2)") != std::string::npos);
  CHECK(j.find("No path exists from any GC root to C (#3)") != std::string::npos);
}

TEST("events.json_escaping") {
  CHECK_EQ(jsonEscape("a\"b\\c\nd"), std::string("a\\\"b\\\\c\\nd"));
  CHECK_EQ(jsonEscape(std::string(1, '\x01')), std::string("\\u0001"));
}

TEST("events.ascii_heap_and_inspector") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  node(rt, "A"); node(rt, "B");
  rt.addRoot("r", rt.resolve("A"));
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  std::string heap = asciiHeap(rt);
  CHECK(heap.find("[A#1@0x1000 32B]") != std::string::npos);
  CHECK(heap.find("[FREE") != std::string::npos);
  std::string info = describeObject(rt, rt.resolve("B"));
  CHECK(info.find("Address:    0x1020") != std::string::npos);
  CHECK(info.find("Referenced by:") != std::string::npos);
  CHECK(info.find("LIVE") != std::string::npos);
}

// ---------------------------------------------------------------- session
TEST("session.basic_commands") {
  Session s;
  CHECK(s.run("new Node 2 0 A").ok);
  CHECK(s.run("new Node 2 0 B").ok);
  CHECK(s.run("ref A 0 B").ok);
  CHECK(s.run("root add r A").ok);
  CHECK(s.run("gc").ok);
  CHECK(s.runtime().exists(1) && s.runtime().exists(2));
  CHECK(!s.run("ref A 9 B").ok);                 // no such slot
  CHECK(!s.run("ref A 0 nope").ok);
  CHECK(!s.run("root add r A").ok);              // duplicate
  CHECK(!s.run("bogus").ok);
  CHECK(!s.run("new Node 2 0 A").ok);            // label in use
  CHECK(s.run("# a comment").ok);
}

TEST("session.gc_commands_are_flagged_for_replay") {
  Session s;
  s.run("new Node 2 0 A");
  CHECK(!s.run("new Node 2 0 B").replay);
  CommandResult r = s.run("gc");
  CHECK(r.replay);
  CHECK(r.eventEnd > r.eventStart);
  std::string j = s.runJson("gc");
  CHECK(j.find("\"before\":") != std::string::npos);
  CHECK(j.find("\"after\":") != std::string::npos);
  CHECK(j.find("\"replay\":true") != std::string::npos);
  CHECK(balanced(j));
}

TEST("session.algorithm_switching") {
  Session s;
  CHECK(s.run("new Node 2 0 A").ok);
  CHECK(s.run("algo compact").message.find("preserved") != std::string::npos);
  CHECK(s.runtime().exists(1));
  CHECK(s.run("algo copying").message.find("reset") != std::string::npos);
  CHECK(!s.runtime().exists(1));
  CHECK(!s.run("algo nonsense").ok);
}

TEST("session.configuration_commands") {
  Session s;
  CHECK(s.run("set promotion 5").ok);
  CHECK_EQ(s.runtime().config.promotionThreshold, 5u);
  CHECK(s.run("set cards on").ok);
  CHECK(s.runtime().config.useCardTable);
  CHECK(s.run("set heap 4096").ok);
  CHECK_EQ(s.runtime().heap.size(), 4096u);
  CHECK(!s.run("set heap 100").ok);
  CHECK(!s.run("set promotion 99").ok);
}

TEST("session.demo1_basic_garbage") {
  Session s;
  CHECK(s.run("demo 1").ok);
  Runtime& rt = s.runtime();
  CHECK(alive(rt, "C") && alive(rt, "D"));       // not yet collected
  s.run("gc");
  CHECK(alive(rt, "A") && alive(rt, "B"));
  CHECK(!alive(rt, "C") && !alive(rt, "D"));
  CHECK_CONSISTENT(rt);
}

TEST("session.demo2_cycle_defeats_reference_counting_but_not_tracing") {
  Session s;
  CHECK(s.run("demo 2").ok);
  Runtime& rt = s.runtime();
  CHECK_EQ(hdr(rt, "A").refCount, 1u);
  CHECK_EQ(hdr(rt, "B").refCount, 1u);
  s.run("gc");
  CHECK(alive(rt, "A") && alive(rt, "B"));
  CHECK_EQ(count(rt, EventType::CycleDetected), 1u);
  CHECK(s.runJson("heap").find("\"cycles\":[[1,2]]") != std::string::npos);

  CHECK(s.run("demo 2 marksweep").ok);
  s.run("gc");
  CHECK_EQ(rt.objects().size(), 0u);
}

TEST("session.demo3_compaction") {
  Session s;
  CHECK(s.run("demo 3").ok);
  Runtime& rt = s.runtime();
  CHECK(rt.config.algo == Algo::MarkCompact);
  CHECK(rt.heap.stats().freeBlocks > 1u);        // holes from the earlier sweep
  s.run("gc");
  CHECK_EQ(rt.heap.stats().freeBlocks, 1u);
  CHECK_EQ(addr(rt, "B"), rt.heap.base() + 32u);
  CHECK_EQ(addr(rt, "C"), rt.heap.base() + 64u);
}

TEST("session.demo4_generational_promotion") {
  Session s;
  CHECK(s.run("demo 4").ok);
  Runtime& rt = s.runtime();
  std::vector<std::string> path;
  for (int i = 0; i < 4; ++i) {
    path.push_back(regionOf(rt, "A") + ":" + std::to_string(hdr(rt, "A").age));
    s.run("gc");
  }
  path.push_back(regionOf(rt, "A") + ":" + std::to_string(hdr(rt, "A").age));
  CHECK((path == std::vector<std::string>{"EDEN:0", "SURVIVOR A:1", "SURVIVOR B:2", "OLD:2", "OLD:2"}));
  CHECK(!alive(rt, "junk1") && !alive(rt, "junk2"));
}

TEST("session.demo5_remembered_set") {
  Session s;
  CHECK(s.run("demo 5").ok);
  Runtime& rt = s.runtime();
  CHECK_EQ(regionOf(rt, "O"), std::string("OLD"));
  CHECK_EQ(regionOf(rt, "Y"), std::string("EDEN"));
  CHECK_EQ(rt.barriers.remembered().size(), 1u);
  s.run("gc minor");
  CHECK(alive(rt, "Y"));
  CHECK_CONSISTENT(rt);
}

TEST("session.demo6_copying") {
  Session s;
  CHECK(s.run("demo 6").ok);
  s.run("gc");
  Runtime& rt = s.runtime();
  CHECK(!alive(rt, "x") && !alive(rt, "y"));
  CHECK_EQ(addr(rt, "A"), rt.heap.base() + 1024u);
  CHECK_CONSISTENT(rt);
  CHECK(!s.run("demo 99").ok);
}

TEST("session.every_demo_survives_every_collection_kind") {
  for (int d = 1; d <= 6; ++d)
    for (const char* kind : {"gc", "gc minor", "gc major"}) {
      Session s;
      CHECK(s.run("demo " + std::to_string(d)).ok);
      CHECK(s.run(kind).ok);
      CHECK_CONSISTENT(s.runtime());
    }
}

TEST("session.inspect_and_why") {
  Session s;
  s.run("demo 1");
  CommandResult live = s.run("why B");
  CHECK(live.message.find("LIVE") != std::string::npos);
  CHECK(live.message.find("Root 'rootA' -> A (#1) -> B (#2)") != std::string::npos);
  CommandResult dead = s.run("why D");
  CHECK(dead.message.find("UNREACHABLE") != std::string::npos);
  CHECK(s.run("inspect A").message.find("Type:       Node") != std::string::npos);
}
