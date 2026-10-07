// Phases 4-6: roots & reachability, reference counting, mark-sweep.
#include "test_util.h"

using namespace tu;

// Root -> A -> B -> C   and   D -> E (D, E unrooted)
static void paperGraph(Runtime& rt) {
  for (const char* l : {"A", "B", "C", "D", "E"}) node(rt, l);
  rt.addRoot("root", rt.resolve("A"));
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  rt.setRef(rt.resolve("B"), 0, rt.resolve("C"));
  rt.setRef(rt.resolve("D"), 0, rt.resolve("E"));
}

// ------------------------------------------------------------------ graph
TEST("graph.reachability") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  ObjectGraph g(rt);
  std::set<ObjId> live = g.reachable();
  CHECK_EQ(live.size(), 3u);
  for (const char* l : {"A", "B", "C"}) CHECK(live.count(rt.resolve(l)) == 1);
  std::vector<ObjId> dead = g.unreachable();
  CHECK_EQ(dead.size(), 2u);
  CHECK_EQ(dead[0], rt.resolve("D"));
  CHECK_EQ(dead[1], rt.resolve("E"));
}

TEST("graph.dfs_and_bfs_orders_differ") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (const char* l : {"A", "B", "C", "D"}) node(rt, l);
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  rt.setRef(rt.resolve("A"), 1, rt.resolve("C"));
  rt.setRef(rt.resolve("B"), 0, rt.resolve("D"));
  ObjectGraph g(rt);
  ObjId A = rt.resolve("A"), B = rt.resolve("B"), C = rt.resolve("C"), D = rt.resolve("D");
  CHECK((g.dfs(A) == std::vector<ObjId>{A, B, D, C}));
  CHECK((g.bfs(A) == std::vector<ObjId>{A, B, C, D}));
}

TEST("graph.explains_live_objects_with_a_path") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  ObjectGraph g(rt);
  std::string why = g.explain(rt.resolve("C"));
  CHECK(why.find("Root 'root'") != std::string::npos);
  CHECK(why.find("A (#1) -> B (#2) -> C (#3)") != std::string::npos);
  std::string rootName;
  std::vector<ObjId> path;
  CHECK(g.pathTo(rt.resolve("C"), rootName, path));
  CHECK_EQ(path.size(), 3u);
}

TEST("graph.explains_unreachable_objects") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  ObjectGraph g(rt);
  CHECK(g.explain(rt.resolve("E")).find("No path exists from any GC root") != std::string::npos);
  std::string rootName;
  std::vector<ObjId> path;
  CHECK(!g.pathTo(rt.resolve("D"), rootName, path));
}

TEST("graph.root_management") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  CHECK(!rt.addRoot("root", rt.resolve("D")));       // duplicate name
  CHECK(rt.addRoot("rD", rt.resolve("D"), 1));       // a stack root of thread 1
  CHECK_EQ(rt.roots.find("rD")->kind == RootKind::Stack, true);
  CHECK_EQ(ObjectGraph(rt).reachable().size(), 5u);
  CHECK(rt.removeRoot("rD"));
  CHECK(!rt.removeRoot("rD"));
  CHECK_EQ(ObjectGraph(rt).reachable().size(), 3u);
}

TEST("graph.self_loop_and_cycle_detection") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (const char* l : {"A", "B", "S"}) node(rt, l);
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  rt.setRef(rt.resolve("B"), 0, rt.resolve("A"));
  rt.setRef(rt.resolve("S"), 0, rt.resolve("S"));
  auto cycles = ObjectGraph(rt).unreachableCycles();
  CHECK_EQ(cycles.size(), 2u);
  CHECK_EQ(cycles[0].size(), 2u);   // {A, B}
  CHECK_EQ(cycles[1].size(), 1u);   // {S}
  rt.addRoot("r", rt.resolve("A"));
  CHECK_EQ(ObjectGraph(rt).unreachableCycles().size(), 1u);   // A-B now reachable
}

// Every tracing collector must agree with the reference reachability analysis.
static void agreesWithReference(Algo algo, CollectKind kind, uint32_t seed) {
  Runtime rt(cfgFor(algo));
  buildRandomGraph(rt, seed, 20);
  std::set<ObjId> expected = ObjectGraph(rt).reachable();
  rt.collect(kind);
  CHECK_CONSISTENT(rt);
  CHECK(allIds(rt) == expected);
  // and a second collection changes nothing
  rt.collect(kind);
  CHECK(allIds(rt) == expected);
  CHECK_CONSISTENT(rt);
}

TEST("graph.marksweep_agrees_with_reference") {
  for (uint32_t seed = 1; seed <= 10; ++seed) agreesWithReference(Algo::MarkSweep, CollectKind::Auto, seed);
}
TEST("graph.markcompact_agrees_with_reference") {
  for (uint32_t seed = 1; seed <= 10; ++seed) agreesWithReference(Algo::MarkCompact, CollectKind::Auto, seed);
}
TEST("graph.copying_agrees_with_reference") {
  for (uint32_t seed = 1; seed <= 10; ++seed) agreesWithReference(Algo::Copying, CollectKind::Auto, seed);
}
TEST("graph.generational_major_agrees_with_reference") {
  for (uint32_t seed = 1; seed <= 10; ++seed) agreesWithReference(Algo::Generational, CollectKind::Major, seed);
}
TEST("graph.generational_minor_never_loses_live_objects") {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Runtime rt(cfgFor(Algo::Generational));
    buildRandomGraph(rt, seed, 20);
    std::set<ObjId> expected = ObjectGraph(rt).reachable();
    for (int i = 0; i < 5; ++i) {
      rt.collect(CollectKind::Minor);
      CHECK_CONSISTENT(rt);
      std::set<ObjId> now = allIds(rt);
      for (ObjId id : expected) CHECK(now.count(id) == 1);
    }
    rt.collect(CollectKind::Major);
    CHECK(allIds(rt) == expected);
  }
}

// ------------------------------------------------------------ reference counting
TEST("refcount.counts_follow_stores_and_roots") {
  Runtime rt(cfgFor(Algo::RefCount));
  ObjId a = node(rt, "A"), b = node(rt, "B"), c = node(rt, "C");
  CHECK_EQ(rt.header(a).refCount, 0u);
  rt.addRoot("rA", a);
  CHECK_EQ(rt.header(a).refCount, 1u);
  rt.setRef(a, 0, b);
  CHECK_EQ(rt.header(b).refCount, 1u);
  rt.setRef(c, 0, b);                       // C is garbage-to-be, but still counts
  CHECK_EQ(rt.header(b).refCount, 2u);
  rt.setRef(a, 0, 0);
  CHECK_EQ(rt.header(b).refCount, 1u);
  rt.setRef(a, 1, a);                       // self reference
  CHECK_EQ(rt.header(a).refCount, 2u);
}

TEST("refcount.zero_count_reclaims_immediately_and_recursively") {
  Runtime rt(cfgFor(Algo::RefCount));
  ObjId a = node(rt, "A"), b = node(rt, "B"), c = node(rt, "C");
  rt.addRoot("r", a);
  rt.setRef(a, 0, b);
  rt.setRef(b, 0, c);
  CHECK_EQ(rt.objects().size(), 3u);
  rt.removeRoot("r");                       // A -> 0, then B -> 0, then C -> 0
  CHECK_EQ(rt.objects().size(), 0u);
  CHECK_EQ(count(rt, EventType::ObjectReclaimed), 3u);
  CHECK_EQ(rt.heap.stats().freeBlocks, 1u); // memory fully coalesced again
  CHECK_CONSISTENT(rt);
}

TEST("refcount.shared_object_survives_one_owner_leaving") {
  Runtime rt(cfgFor(Algo::RefCount));
  ObjId a = node(rt, "A"), b = node(rt, "B"), s = node(rt, "S");
  rt.addRoot("ra", a);
  rt.addRoot("rb", b);
  rt.setRef(a, 0, s);
  rt.setRef(b, 0, s);
  CHECK_EQ(rt.header(s).refCount, 2u);
  rt.removeRoot("ra");
  CHECK(!alive(rt, "A"));
  CHECK(alive(rt, "S"));
  CHECK_EQ(rt.header(s).refCount, 1u);
}

TEST("refcount.overwriting_with_same_target_is_safe") {
  Runtime rt(cfgFor(Algo::RefCount));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  rt.addRoot("ra", a);
  rt.setRef(a, 0, b);
  rt.setRef(a, 0, b);                       // increment-before-decrement
  CHECK(alive(rt, "B"));
  CHECK_EQ(rt.header(b).refCount, 1u);
}

TEST("refcount.cycle_is_never_reclaimed") {
  Runtime rt(cfgFor(Algo::RefCount));
  ObjId a = node(rt, "A", 1), b = node(rt, "B", 1);
  rt.addRoot("ext", a);
  rt.setRef(a, 0, b);
  rt.setRef(b, 0, a);
  CHECK_EQ(rt.header(a).refCount, 2u);      // ext + B
  CHECK_EQ(rt.header(b).refCount, 1u);      // A
  rt.removeRoot("ext");
  CHECK_EQ(rt.header(a).refCount, 1u);      // the demo from the spec:
  CHECK_EQ(rt.header(b).refCount, 1u);      // both stay at 1
  CHECK(alive(rt, "A") && alive(rt, "B"));
  rt.collect();                             // RC "GC" cannot fix it...
  CHECK(alive(rt, "A") && alive(rt, "B"));
  CHECK_EQ(count(rt, EventType::CycleDetected), 1u);   // ...but it diagnoses it
  CHECK_EQ(ObjectGraph(rt).unreachable().size(), 2u);
}

TEST("refcount.tracing_gc_reclaims_the_same_cycle") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "A", 1), b = node(rt, "B", 1);
  rt.addRoot("ext", a);
  rt.setRef(a, 0, b);
  rt.setRef(b, 0, a);
  rt.removeRoot("ext");
  rt.collect();
  CHECK_EQ(rt.objects().size(), 0u);
}

TEST("refcount.switching_from_tracing_gc_recomputes_counts") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  rt.addRoot("r", a);
  rt.setRef(a, 0, b);
  rt.setRef(b, 0, b);
  CHECK(rt.setAlgorithm(Algo::RefCount));   // heap preserved
  CHECK_EQ(rt.header(a).refCount, 1u);
  CHECK_EQ(rt.header(b).refCount, 2u);      // from A and from itself
}

TEST("refcount.collect_frees_never_referenced_objects") {
  Runtime rt(cfgFor(Algo::RefCount));
  node(rt, "orphan");
  CHECK_EQ(rt.objects().size(), 1u);
  rt.collect();
  CHECK_EQ(rt.objects().size(), 0u);
}

// ------------------------------------------------------------------ mark-sweep
TEST("marksweep.paper_example") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  rt.collect();
  for (const char* l : {"A", "B", "C"}) CHECK(alive(rt, l));
  for (const char* l : {"D", "E"}) CHECK(!alive(rt, l));
  CHECK_CONSISTENT(rt);
}

TEST("marksweep.live_objects_do_not_move") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  Addr a = addr(rt, "A"), b = addr(rt, "B"), c = addr(rt, "C");
  rt.collect();
  CHECK_EQ(addr(rt, "A"), a);
  CHECK_EQ(addr(rt, "B"), b);
  CHECK_EQ(addr(rt, "C"), c);
}

TEST("marksweep.event_sequence_matches_the_story") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  rt.events.clearHistory();
  rt.collect();
  CHECK(containsInOrder(rt, {EventType::SafepointRequested, EventType::AllThreadsStopped, EventType::GCStarted,
                             EventType::RootDiscovered, EventType::ObjectDiscovered, EventType::ObjectMarked,
                             EventType::ObjectScanned, EventType::ReferenceTraversed, EventType::MarkingCompleted,
                             EventType::ObjectUnreachable, EventType::ObjectReclaimed, EventType::FreeListRebuilt,
                             EventType::GCCompleted, EventType::ThreadsResumed}));
  CHECK_EQ(count(rt, EventType::ObjectReclaimed), 2u);
  CHECK_EQ(count(rt, EventType::ObjectUnreachable), 2u);
  CHECK_EQ(count(rt, EventType::ObjectMarked), 3u);
}

TEST("marksweep.breadth_first_discovery_order") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  rt.events.clearHistory();
  rt.collect();
  std::vector<ObjId> discovered;
  for (const Event& e : rt.events.history())
    if (e.type == EventType::ObjectDiscovered) discovered.push_back(e.id);
  CHECK((discovered == std::vector<ObjId>{1, 2, 3}));   // A, then B (via A), then C (via B)
}

TEST("marksweep.unreachable_events_carry_the_reason") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  rt.events.clearHistory();
  rt.collect();
  bool found = false;
  for (const Event& e : rt.events.history())   // D has id 4
    if (e.type == EventType::ObjectUnreachable && e.id == 4) {
      found = true;
      CHECK(e.text.find("no path exists from any GC root") != std::string::npos);
    }
  CHECK(found);
}

TEST("marksweep.colors_reset_after_cycle") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  rt.collect();
  for (const auto& kv : rt.objects()) CHECK_EQ(rt.heap.mem().header(kv.second)->color, kWhite);
}

TEST("marksweep.cycles_and_self_references_are_collected") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (const char* l : {"A", "B", "S"}) node(rt, l);
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  rt.setRef(rt.resolve("B"), 0, rt.resolve("A"));
  rt.setRef(rt.resolve("S"), 0, rt.resolve("S"));
  rt.collect();
  CHECK_EQ(rt.objects().size(), 0u);
  CHECK_EQ(rt.heap.stats().freeBlocks, 1u);
}

TEST("marksweep.free_list_is_coalesced_after_sweep") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (int i = 0; i < 6; ++i) node(rt, "");
  rt.addRoot("r", rt.resolve("A"));
  rt.collect();                              // B..F were adjacent garbage + the tail: one block
  CHECK_EQ(rt.heap.stats().freeBlocks, 1u);
  CHECK_EQ(rt.heap.stats().used, 32u);
}

TEST("marksweep.statistics") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  paperGraph(rt);
  rt.collect();
  CHECK_EQ(rt.metrics.collections, 1);
  CHECK_EQ(rt.metrics.last.reclaimedObjects, 2);
  CHECK_EQ(rt.metrics.last.reclaimedBytes, 64);
  CHECK_EQ(rt.metrics.last.marked, 3);
}

TEST("marksweep.age_is_irrelevant_only_reachability_matters") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "A");
  rt.addRoot("r", a);
  for (int i = 0; i < 5; ++i) rt.collect();  // A is "old" now...
  CHECK(alive(rt, "A"));
  rt.removeRoot("r");
  rt.collect();                              // ...but unreachable, so it is collected anyway
  CHECK(!alive(rt, "A"));
}
