// Phases 7-8: mark-compact and copying collectors.
#include "test_util.h"

using namespace tu;

// A x B y C laid out side by side; x and y are garbage; A->B->C.
static void holes(Runtime& rt) {
  for (const char* l : {"A", "x", "B", "y", "C"}) node(rt, l, 1);
  rt.addRoot("rA", rt.resolve("A"));
  rt.setRef(rt.resolve("A"), 0, rt.resolve("B"));
  rt.setRef(rt.resolve("B"), 0, rt.resolve("C"));
}

// ------------------------------------------------------------------ compact
TEST("compact.slides_live_objects_together") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  holes(rt);
  Addr base = rt.heap.base();
  CHECK_EQ(addr(rt, "B"), base + 64u);
  CHECK_EQ(addr(rt, "C"), base + 128u);
  rt.collect();
  CHECK(!alive(rt, "x") && !alive(rt, "y"));
  CHECK_EQ(addr(rt, "A"), base);               // A did not need to move
  CHECK_EQ(addr(rt, "B"), base + 32u);
  CHECK_EQ(addr(rt, "C"), base + 64u);
  CHECK_CONSISTENT(rt);
}

TEST("compact.free_space_becomes_one_block") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  holes(rt);
  rt.collect();
  RegionStats s = rt.heap.stats();
  CHECK_EQ(s.freeBlocks, 1u);
  CHECK_EQ(s.largestFree, 2048u - 96u);
  CHECK_EQ(s.fragmentation, 0.0);
  // allocation continues right after the live data
  ObjId d = node(rt, "D");
  CHECK_EQ(rt.addrOf(d), rt.heap.base() + 96u);
}

TEST("compact.references_and_roots_are_updated") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  holes(rt);
  Addr oldB = addr(rt, "B");
  Addr oldC = addr(rt, "C");
  rt.addRoot("rC", rt.resolve("C"));           // a root that must move with C
  rt.collect();
  CHECK(addr(rt, "B") != oldB);
  CHECK_EQ(rawTarget(rt, "A", 0), addr(rt, "B"));   // A.refs[0] rewritten
  CHECK_EQ(rawTarget(rt, "B", 0), addr(rt, "C"));
  CHECK_EQ(rt.roots.find("rC")->addr, addr(rt, "C"));
  CHECK(addr(rt, "C") != oldC);
  CHECK_CONSISTENT(rt);
}

TEST("compact.events_show_old_and_new_addresses") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  holes(rt);
  Addr oldB = addr(rt, "B");
  rt.events.clearHistory();
  rt.collect();
  CHECK_EQ(count(rt, EventType::ForwardingAssigned), 3u);   // A, B, C
  CHECK_EQ(count(rt, EventType::ObjectMoved), 2u);          // A stays, B and C move
  bool sawB = false;
  for (const Event& e : rt.events.history())
    if (e.type == EventType::ObjectMoved && e.id == 3) {    // B has id 3
      sawB = true;
      CHECK_EQ(e.addr, oldB);
      CHECK_EQ(e.addr2, rt.heap.base() + 32u);
    }
  CHECK(sawB);
  CHECK_EQ(count(rt, EventType::ReferenceUpdated), 2u);     // A.refs[0] and B.refs[0]
  CHECK(containsInOrder(rt, {EventType::GCStarted, EventType::MarkingCompleted, EventType::ForwardingAssigned,
                             EventType::ReferenceUpdated, EventType::ObjectMoved, EventType::FreeListRebuilt,
                             EventType::GCCompleted}));
}

TEST("compact.payload_survives_relocation") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  node(rt, "junk");
  ObjId b = rt.allocate("Blob", 0, 16, "B");
  rt.addRoot("r", b);
  uint8_t* payload = rt.heap.mem().raw(rt.addrOf(b)) + kHeaderSize;
  for (int i = 0; i < 16; ++i) payload[i] = static_cast<uint8_t>(0xA0 + i);
  rt.collect();                                   // junk dies, B slides down
  CHECK_EQ(rt.addrOf(b), rt.heap.base());
  payload = rt.heap.mem().raw(rt.addrOf(b)) + kHeaderSize;
  for (int i = 0; i < 16; ++i) CHECK_EQ(payload[i], static_cast<uint8_t>(0xA0 + i));
}

TEST("compact.already_compact_heap_is_untouched") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  for (const char* l : {"A", "B", "C"}) { node(rt, l); rt.addRoot(std::string("r") + l, rt.resolve(l)); }
  rt.events.clearHistory();
  rt.collect();
  CHECK_EQ(count(rt, EventType::ObjectMoved), 0u);
  CHECK_EQ(count(rt, EventType::ObjectReclaimed), 0u);
  CHECK_CONSISTENT(rt);
}

TEST("compact.preserves_relative_order") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  buildRandomGraph(rt, 7, 20, 3);
  std::vector<ObjId> before;
  for (const auto& kv : rt.objects()) before.push_back(kv.first);
  std::set<ObjId> live = ObjectGraph(rt).reachable();
  rt.collect();
  Addr prev = 0;
  for (ObjId id : before) {                          // ids were allocated in address order
    if (!live.count(id)) continue;
    CHECK(rt.addrOf(id) > prev);
    prev = rt.addrOf(id);
  }
  CHECK_CONSISTENT(rt);
}

TEST("compact.after_marksweep_fragmentation_is_removed") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  holes(rt);
  rt.collect();                                    // mark-sweep leaves holes
  CHECK(rt.heap.stats().freeBlocks > 1u);
  CHECK(rt.setAlgorithm(Algo::MarkCompact));       // same heap layout, keep the heap
  rt.collect();
  CHECK_EQ(rt.heap.stats().freeBlocks, 1u);
  CHECK_EQ(addr(rt, "B"), rt.heap.base() + 32u);
  CHECK_CONSISTENT(rt);
}

TEST("compact.cycles_survive_and_are_updated") {
  Runtime rt(cfgFor(Algo::MarkCompact));
  node(rt, "junk");
  ObjId a = node(rt, "A", 1), b = node(rt, "B", 1);
  rt.setRef(a, 0, b);
  rt.setRef(b, 0, a);
  rt.addRoot("r", a);
  rt.collect();
  CHECK_EQ(target(rt, "A", 0), b);
  CHECK_EQ(target(rt, "B", 0), a);
  CHECK_CONSISTENT(rt);
}

// ------------------------------------------------------------------ copying
TEST("copying.layout_is_two_semispaces") {
  Runtime rt(cfgFor(Algo::Copying));
  CHECK_EQ(rt.heap.regions().size(), 2u);
  CHECK(rt.heap.region(RegionKind::FromSpace) != nullptr);
  CHECK(rt.heap.region(RegionKind::ToSpace) != nullptr);
  CHECK_EQ(rt.heap.region(RegionKind::FromSpace)->size(), 1024u);
  ObjId a = node(rt, "A");
  CHECK_EQ(rt.spaceName(rt.addrOf(a)), std::string("FROM-SPACE"));
}

TEST("copying.live_objects_end_up_contiguous_in_the_other_space") {
  Runtime rt(cfgFor(Algo::Copying));
  holes(rt);
  Addr half = rt.heap.base() + 1024u;
  rt.collect();
  CHECK(!alive(rt, "x") && !alive(rt, "y"));
  CHECK_EQ(addr(rt, "A"), half);
  CHECK_EQ(addr(rt, "B"), half + 32u);
  CHECK_EQ(addr(rt, "C"), half + 64u);
  CHECK_EQ(regionOf(rt, "A"), std::string("FROM-SPACE"));   // roles swapped: it is the new from-space
  CHECK_CONSISTENT(rt);
}

TEST("copying.no_fragmentation_by_construction") {
  Runtime rt(cfgFor(Algo::Copying));
  for (int i = 0; i < 20; ++i) node(rt, "");
  for (int i = 1; i <= 20; i += 3) rt.addRoot("r" + std::to_string(i), static_cast<ObjId>(i));
  rt.collect();
  Region* from = rt.heap.region(RegionKind::FromSpace);
  RegionStats s = rt.heap.regionStats(*from);
  CHECK_EQ(s.freeBlocks, 1u);                       // everything free is one tail block
  CHECK_EQ(s.fragmentation, 0.0);
  CHECK_EQ(s.used + s.free, 1024u);
}

TEST("copying.forwarding_pointers_and_updated_references") {
  Runtime rt(cfgFor(Algo::Copying));
  holes(rt);
  rt.events.clearHistory();
  rt.collect();
  CHECK_EQ(count(rt, EventType::ObjectMoved), 3u);          // A, B, C each copied exactly once
  CHECK_EQ(count(rt, EventType::ObjectReclaimed), 2u);      // x and y
  CHECK_EQ(rawTarget(rt, "A", 0), addr(rt, "B"));
  CHECK_EQ(rawTarget(rt, "B", 0), addr(rt, "C"));
  CHECK_EQ(rt.roots.find("rA")->addr, addr(rt, "A"));
  CHECK(containsInOrder(rt, {EventType::GCStarted, EventType::RootDiscovered, EventType::ObjectMoved,
                             EventType::ReferenceUpdated, EventType::ObjectScanned, EventType::ObjectReclaimed,
                             EventType::SpacesSwapped, EventType::GCCompleted}));
}

TEST("copying.spaces_alternate") {
  Runtime rt(cfgFor(Algo::Copying));
  holes(rt);
  Addr first = addr(rt, "A");
  rt.collect();
  Addr second = addr(rt, "A");
  rt.collect();
  Addr third = addr(rt, "A");
  CHECK(first != second);
  CHECK_EQ(first, third);                           // back in the original space
  CHECK_CONSISTENT(rt);
}

TEST("copying.shared_and_cyclic_objects_are_copied_once") {
  Runtime rt(cfgFor(Algo::Copying));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  rt.setRef(a, 0, b);
  rt.setRef(a, 1, b);
  rt.setRef(b, 0, a);
  rt.addRoot("r1", a);
  rt.addRoot("r2", b);                              // second root to an already-copied object
  rt.events.clearHistory();
  rt.collect();
  CHECK_EQ(count(rt, EventType::ObjectMoved), 2u);
  CHECK_EQ(rawTarget(rt, "A", 0), rawTarget(rt, "A", 1));
  CHECK_EQ(target(rt, "B", 0), a);
  CHECK_EQ(rt.roots.find("r2")->addr, addr(rt, "B"));
  CHECK_CONSISTENT(rt);
}

TEST("copying.allocation_continues_after_the_survivors") {
  Runtime rt(cfgFor(Algo::Copying));
  holes(rt);
  rt.collect();
  ObjId d = node(rt, "D");
  CHECK_EQ(rt.addrOf(d), rt.heap.base() + 1024u + 96u);
}

TEST("copying.auto_gc_when_the_space_fills") {
  Runtime rt(cfgFor(Algo::Copying));
  ObjId keep = node(rt, "keep");
  rt.addRoot("r", keep);
  for (int i = 0; i < 300; ++i) CHECK(rt.allocate("Node", 2, 0, "") != 0);
  CHECK(rt.metrics.collections >= 1);
  CHECK(alive(rt, "keep"));
  CHECK_CONSISTENT(rt);
}

TEST("copying.payload_survives_the_copy") {
  Runtime rt(cfgFor(Algo::Copying));
  node(rt, "junk");
  ObjId b = rt.allocate("Blob", 0, 24, "B");
  rt.addRoot("r", b);
  uint8_t* p = rt.heap.mem().raw(rt.addrOf(b)) + kHeaderSize;
  for (int i = 0; i < 24; ++i) p[i] = static_cast<uint8_t>(i * 3);
  rt.collect();
  p = rt.heap.mem().raw(rt.addrOf(b)) + kHeaderSize;
  for (int i = 0; i < 24; ++i) CHECK_EQ(p[i], static_cast<uint8_t>(i * 3));
}
