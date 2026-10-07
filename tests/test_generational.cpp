// Phases 9-10: generational collector, write barrier, remembered set, card table.
#include "minigc/gc_algorithms.h"
#include "test_util.h"

using namespace tu;

static Runtime& promoteToOld(Runtime& rt, const std::string& label, int gcs = 3) {
  for (int i = 0; i < gcs; ++i) rt.collect(CollectKind::Minor);
  CHECK_EQ(regionOf(rt, label), std::string("OLD"));
  return rt;
}

// ------------------------------------------------------------------ layout
TEST("generational.layout_has_eden_two_survivors_and_old") {
  Runtime rt(cfgFor(Algo::Generational));
  CHECK_EQ(rt.heap.regions().size(), 4u);
  CHECK_EQ(rt.heap.region(RegionKind::Eden)->size(), 768u);
  CHECK_EQ(rt.heap.region(RegionKind::SurvivorA)->size(), 256u);
  CHECK_EQ(rt.heap.region(RegionKind::SurvivorB)->size(), 256u);
  CHECK_EQ(rt.heap.region(RegionKind::Old)->size(), 768u);
  CHECK_EQ(std::string(rt.heap.region(RegionKind::Eden)->alloc->kind()), std::string("bump"));
  CHECK_EQ(std::string(rt.heap.region(RegionKind::Old)->alloc->kind()), std::string("free-list"));
}

TEST("generational.new_objects_start_in_eden_with_age_zero") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId a = node(rt, "A");
  CHECK_EQ(rt.spaceName(rt.addrOf(a)), std::string("EDEN"));
  CHECK_EQ(rt.header(a).age, 0u);
}

TEST("generational.large_objects_bypass_eden") {
  Runtime rt(cfgFor(Algo::Generational));
  // 24 + 400 = 424 bytes: larger than a survivor space (256), so it is allocated in Old directly.
  ObjId big = rt.allocate("Blob", 0, 400, "big");
  CHECK(big != 0);
  CHECK_EQ(rt.spaceName(rt.addrOf(big)), std::string("OLD"));
  ObjId tiny = node(rt, "tiny");
  CHECK_EQ(rt.spaceName(rt.addrOf(tiny)), std::string("EDEN"));
}

// -------------------------------------------------------------- minor GC
TEST("generational.minor_gc_reclaims_young_garbage") {
  Runtime rt(cfgFor(Algo::Generational));
  node(rt, "A");
  node(rt, "junk1");
  node(rt, "junk2");
  rt.addRoot("rA", rt.resolve("A"));
  rt.collect(CollectKind::Minor);
  CHECK(alive(rt, "A"));
  CHECK(!alive(rt, "junk1") && !alive(rt, "junk2"));
  CHECK_EQ(rt.heap.region(RegionKind::Eden)->alloc->walkLimit(), rt.heap.region(RegionKind::Eden)->start);   // Eden empty
  CHECK_CONSISTENT(rt);
}

TEST("generational.survivors_age_and_are_promoted") {
  Runtime rt(cfgFor(Algo::Generational));            // promotion threshold = 2
  ObjId a = node(rt, "A");
  rt.addRoot("rA", a);
  CHECK_EQ(regionOf(rt, "A"), std::string("EDEN"));
  CHECK_EQ(rt.header(a).age, 0u);

  rt.collect(CollectKind::Minor);                    // Eden -> Survivor A
  CHECK_EQ(regionOf(rt, "A"), std::string("SURVIVOR A"));
  CHECK_EQ(hdr(rt, "A").age, 1u);

  rt.collect(CollectKind::Minor);                    // Survivor A -> Survivor B
  CHECK_EQ(regionOf(rt, "A"), std::string("SURVIVOR B"));
  CHECK_EQ(hdr(rt, "A").age, 2u);

  rt.events.clearHistory();
  rt.collect(CollectKind::Minor);                    // age reached the threshold: -> Old
  CHECK_EQ(regionOf(rt, "A"), std::string("OLD"));
  CHECK_EQ(count(rt, EventType::ObjectPromoted), 1u);
  CHECK_CONSISTENT(rt);

  rt.collect(CollectKind::Minor);                    // old objects are not touched by minor GCs
  CHECK_EQ(regionOf(rt, "A"), std::string("OLD"));
}

TEST("generational.promotion_threshold_is_configurable") {
  Config c = cfgFor(Algo::Generational);
  c.promotionThreshold = 0;
  Runtime rt(c);
  ObjId a = node(rt, "A");
  rt.addRoot("rA", a);
  rt.collect(CollectKind::Minor);
  CHECK_EQ(regionOf(rt, "A"), std::string("SURVIVOR A"));
  rt.collect(CollectKind::Minor);
  CHECK_EQ(regionOf(rt, "A"), std::string("OLD"));
}

TEST("generational.minor_gc_updates_references_between_young_objects") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId a = node(rt, "A"), b = node(rt, "B"), c = node(rt, "C");
  rt.addRoot("rA", a);
  rt.setRef(a, 0, b);
  rt.setRef(b, 0, c);
  rt.setRef(c, 0, a);                                // a cycle among young objects
  rt.collect(CollectKind::Minor);
  CHECK_EQ(target(rt, "A", 0), b);
  CHECK_EQ(target(rt, "B", 0), c);
  CHECK_EQ(target(rt, "C", 0), a);
  CHECK_EQ(rawTarget(rt, "A", 0), addr(rt, "B"));
  CHECK_EQ(rt.roots.find("rA")->addr, addr(rt, "A"));
  CHECK_CONSISTENT(rt);
}

TEST("generational.both_survivor_spaces_alternate_roles") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId a = node(rt, "A");
  rt.addRoot("rA", a);
  rt.collect(CollectKind::Minor);
  Region* used1 = static_cast<GenerationalCollector*>(rt.collector.get())->survivorInUse();
  rt.collect(CollectKind::Minor);
  Region* used2 = static_cast<GenerationalCollector*>(rt.collector.get())->survivorInUse();
  CHECK(used1 != used2);
  CHECK_EQ(rt.heap.regionStats(*used1).objects, 0u);   // the previous survivor space is empty again
}

TEST("generational.survivor_overflow_promotes_early") {
  Runtime rt(cfgFor(Algo::Generational));            // a survivor space holds 8 objects of 32 bytes
  for (int i = 0; i < 12; ++i) {
    ObjId id = node(rt, "");
    rt.addRoot("r" + std::to_string(i), id);
  }
  rt.collect(CollectKind::Minor);
  size_t inSurvivor = 0, inOld = 0;
  for (const auto& kv : rt.objects()) {
    std::string r = rt.spaceName(kv.second);
    if (r == "SURVIVOR A") ++inSurvivor;
    if (r == "OLD") ++inOld;
  }
  CHECK_EQ(inSurvivor, 8u);
  CHECK_EQ(inOld, 4u);
  CHECK_CONSISTENT(rt);
}

TEST("generational.eden_exhaustion_triggers_minor_gc_automatically") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId keep = node(rt, "keep");
  rt.addRoot("r", keep);
  for (int i = 0; i < 200; ++i) CHECK(rt.allocate("Node", 2, 0, "") != 0);
  CHECK(rt.metrics.minorCollections >= 1);
  CHECK(alive(rt, "keep"));
  CHECK_CONSISTENT(rt);
}

// -------------------------------------------------------------- major GC
TEST("generational.major_gc_reclaims_old_garbage_that_minor_cannot") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  rt.setRef(a, 0, b);
  rt.addRoot("rA", a);
  promoteToOld(rt, "A");
  CHECK_EQ(regionOf(rt, "B"), std::string("OLD"));
  rt.removeRoot("rA");                               // A and B are now old garbage
  rt.collect(CollectKind::Minor);
  CHECK(alive(rt, "A") && alive(rt, "B"));           // a minor GC never looks at the old generation
  rt.collect(CollectKind::Major);
  CHECK(!alive(rt, "A") && !alive(rt, "B"));
  CHECK_EQ(rt.heap.regionStats(*rt.heap.region(RegionKind::Old)).freeBlocks, 1u);
  CHECK_CONSISTENT(rt);
}

TEST("generational.major_gc_handles_young_and_old_together") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y"), z = node(rt, "Z");
  rt.setRef(o, 0, y);
  CHECK(alive(rt, "Z"));
  rt.collect(CollectKind::Major);
  CHECK(alive(rt, "O") && alive(rt, "Y"));
  CHECK(!alive(rt, "Z"));
  CHECK_EQ(target(rt, "O", 0), y);
  CHECK_EQ(rawTarget(rt, "O", 0), addr(rt, "Y"));
  CHECK(rt.barriers.remembered().count(o) == 1);     // Y is still young: O must stay remembered
  (void)z;
  CHECK_CONSISTENT(rt);
}

TEST("generational.major_gc_agrees_after_many_cycles") {
  Runtime rt(cfgFor(Algo::Generational));
  buildRandomGraph(rt, 42, 20, 5);
  for (int i = 0; i < 6; ++i) {
    rt.collect(i % 3 == 2 ? CollectKind::Major : CollectKind::Minor);
    CHECK_CONSISTENT(rt);
    for (ObjId id : ObjectGraph(rt).reachable()) CHECK(rt.exists(id));
  }
}

// ------------------------------------------------------- write barrier etc
TEST("barrier.old_to_young_store_is_recorded") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.events.clearHistory();
  rt.setRef(o, 0, y);
  CHECK_EQ(count(rt, EventType::WriteBarrierTriggered), 1u);
  CHECK_EQ(count(rt, EventType::RememberedSetAdded), 1u);
  CHECK_EQ(count(rt, EventType::CardDirtied), 1u);
  CHECK(rt.barriers.remembered().count(o) == 1);
  size_t card = rt.barriers.cards().indexOf(addr(rt, "O"));
  CHECK(rt.barriers.cards().dirty(card));
  CHECK(containsInOrder(rt, {EventType::ReferenceStored, EventType::WriteBarrierTriggered,
                             EventType::CardDirtied, EventType::RememberedSetAdded}));
}

TEST("barrier.young_to_young_and_old_to_old_stores_are_ignored") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  rt.setRef(a, 0, b);                                // young -> young
  CHECK_EQ(count(rt, EventType::WriteBarrierTriggered), 0u);
  rt.addRoot("rA", a);
  rt.addRoot("rB", b);
  promoteToOld(rt, "A");
  rt.events.clearHistory();
  rt.setRef(a, 1, b);                                // old -> old
  CHECK_EQ(count(rt, EventType::WriteBarrierTriggered), 0u);
  CHECK(rt.barriers.remembered().empty());
}

TEST("barrier.null_stores_and_young_to_old_stores_are_ignored") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.setRef(o, 0, 0);
  rt.setRef(y, 0, o);                                // young -> old needs no barrier
  CHECK_EQ(count(rt, EventType::WriteBarrierTriggered), 0u);
}

TEST("barrier.remembered_set_keeps_young_object_alive_through_minor_gc") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.setRef(o, 0, y);                                // Y is reachable ONLY through the old object
  rt.events.clearHistory();
  rt.collect(CollectKind::Minor);
  CHECK(alive(rt, "Y"));
  CHECK_EQ(regionOf(rt, "Y"), std::string("SURVIVOR B"));   // 3 earlier minors: the 4th copies into B
  CHECK_EQ(rawTarget(rt, "O", 0), addr(rt, "Y"));    // old object's field was rewritten
  CHECK(rt.barriers.remembered().count(o) == 1);     // still points at a young object: stay remembered
  CHECK(containsInOrder(rt, {EventType::MinorGCStarted, EventType::ObjectScanned, EventType::ReferenceTraversed,
                             EventType::ObjectMoved, EventType::ReferenceUpdated, EventType::MinorGCCompleted}));
  CHECK_CONSISTENT(rt);
}

TEST("barrier.without_the_barrier_the_young_object_would_be_lost") {
  // Demonstrates WHY the barrier exists: bypass it by writing the field directly.
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.heap.mem().refs(addr(rt, "O"))[0] = rt.addrOf(y);   // raw store: no barrier
  CHECK(rt.barriers.remembered().empty());
  rt.collect(CollectKind::Minor);
  CHECK(!alive(rt, "Y"));                                // reclaimed although O still points at it
}

TEST("barrier.entry_is_dropped_once_the_young_target_is_promoted") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.setRef(o, 0, y);
  for (int i = 0; i < 3; ++i) rt.collect(CollectKind::Minor);   // Y: eden -> S -> S -> old
  CHECK_EQ(regionOf(rt, "Y"), std::string("OLD"));
  CHECK(rt.barriers.remembered().empty());
  CHECK(rt.barriers.cards().dirtyCards().empty());
  CHECK_CONSISTENT(rt);
}

TEST("barrier.entry_is_dropped_when_the_reference_is_cleared") {
  Runtime rt(cfgFor(Algo::Generational));
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.setRef(o, 0, y);
  rt.setRef(o, 0, 0);
  rt.collect(CollectKind::Minor);
  CHECK(!alive(rt, "Y"));
  CHECK(rt.barriers.remembered().empty());
}

TEST("barrier.promoted_object_pointing_to_young_object_becomes_remembered") {
  Config c = cfgFor(Algo::Generational);
  c.promotionThreshold = 0;
  Runtime rt2(c);
  ObjId p = node(rt2, "P");
  rt2.addRoot("rP", p);
  rt2.collect(CollectKind::Minor);                    // P -> Survivor A (age 1)
  ObjId q = node(rt2, "Q");
  rt2.setRef(p, 0, q);                             // Q is a brand-new young object (Eden)
  rt2.collect(CollectKind::Minor);                    // P promoted; Q copied to a survivor
  CHECK_EQ(regionOf(rt2, "P"), std::string("OLD"));
  CHECK_EQ(regionOf(rt2, "Q"), std::string("SURVIVOR B"));
  CHECK(rt2.barriers.remembered().count(p) == 1);
}

TEST("barrier.card_table_mode_finds_the_same_roots") {
  Config c = cfgFor(Algo::Generational);
  c.useCardTable = true;
  Runtime rt(c);
  ObjId o = node(rt, "O");
  rt.addRoot("rO", o);
  promoteToOld(rt, "O");
  ObjId y = node(rt, "Y");
  rt.setRef(o, 0, y);
  CHECK(!rt.barriers.cards().dirtyCards().empty());
  rt.collect(CollectKind::Minor);
  CHECK(alive(rt, "Y"));
  CHECK_EQ(regionOf(rt, "Y"), std::string("SURVIVOR B"));
  CHECK_CONSISTENT(rt);
}

TEST("barrier.card_index_is_derived_from_the_address") {
  CardTable t;
  t.configure(0x1400, 768, 128);
  CHECK_EQ(t.cardCount(), 6u);
  CHECK_EQ(t.indexOf(0x1400), 0u);
  CHECK_EQ(t.indexOf(0x147F), 0u);
  CHECK_EQ(t.indexOf(0x1480), 1u);
  CHECK(t.markDirty(0x1490));                          // was clean
  CHECK(!t.markDirty(0x14A0));                         // same card, already dirty
  CHECK_EQ(t.dirtyCards().size(), 1u);
  t.clear();
  CHECK(t.dirtyCards().empty());
}

TEST("barrier.only_the_generational_collector_uses_barriers") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  rt.setRef(a, 0, b);
  CHECK_EQ(count(rt, EventType::WriteBarrierTriggered), 0u);
  CHECK(rt.barriers.remembered().empty());
}
