// Phases 1-3: raw heap, allocators, object model.
#include "test_util.h"

using namespace tu;

// ------------------------------------------------------------------ heap
TEST("heap.reserve_and_bounds") {
  Heap heap(0x1000, 2048);
  CHECK_EQ(heap.base(), 0x1000u);
  CHECK_EQ(heap.end(), 0x1800u);
  CHECK_EQ(heap.size(), 2048u);
  Region& r = heap.addRegion(RegionKind::Main, "HEAP", 0x1000, 2048, false);
  CHECK(heap.regionOf(0x1000) == &r);
  CHECK(heap.regionOf(0x17F8) == &r);
  CHECK(heap.regionOf(0x1800) == nullptr);
  CHECK(heap.regionOf(0x0FF8) == nullptr);
}

TEST("heap.address_validation") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "A");
  Addr aa = rt.addrOf(a);
  CHECK(rt.heap.isObject(aa));
  CHECK(!rt.heap.isObject(aa + 4));      // misaligned
  CHECK(!rt.heap.isObject(0));           // null
  CHECK(!rt.heap.isObject(0x9000));      // outside the heap
  CHECK(!rt.heap.isObject(aa + 64));     // inside the heap, but a free block
}

TEST("heap.rejects_bad_geometry") {
  bool threw = false;
  try { Heap h(0x1000, 100); } catch (const std::exception&) { threw = true; }
  CHECK(threw);
}

TEST("heap.statistics") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  node(rt, "A"); node(rt, "B"); node(rt, "C");
  RegionStats s = rt.heap.stats();
  CHECK_EQ(s.size, 2048u);
  CHECK_EQ(s.used, 96u);
  CHECK_EQ(s.free, 2048u - 96u);
  CHECK_EQ(s.objects, 3u);
  CHECK_EQ(s.freeBlocks, 1u);
}

// ------------------------------------------------------------- allocators
TEST("allocator.bump_alignment_and_order") {
  Memory mem(0x1000, 1024);
  BumpAllocator b(mem, 0x1000, 0x1400);
  Addr a = b.allocate(10);   // rounded up to 16
  Addr c = b.allocate(24);
  CHECK_EQ(a, 0x1000u);
  CHECK_EQ(c, 0x1010u);
  CHECK_EQ(a % 8, 0u);
  CHECK_EQ(b.cursor(), 0x1028u);
  CHECK_EQ(b.freeBytes(), 1024u - 40u);
}

TEST("allocator.bump_exhaustion_and_reset") {
  Memory mem(0x1000, 64);
  BumpAllocator b(mem, 0x1000, 0x1040);
  CHECK(b.allocate(32) != 0);
  CHECK(b.allocate(32) != 0);
  CHECK_EQ(b.allocate(8), 0u);   // full
  b.reset();
  CHECK(b.allocate(64) != 0);
}

TEST("allocator.freelist_splits_blocks") {
  Memory mem(0x1000, 1024);
  FreeListAllocator fl(mem, 0x1000, 0x1400);
  CHECK_EQ(fl.freeBlockCount(), 1u);
  uint32_t actual = 0;
  Addr a = fl.allocateBlock(32, actual);
  CHECK_EQ(a, 0x1000u);
  CHECK_EQ(actual, 32u);
  CHECK_EQ(fl.freeBlockCount(), 1u);          // remainder is the new single free block
  CHECK_EQ(fl.freeBytes(), 1024u - 32u);
  CHECK_EQ(fl.splits(), 1u);
  CHECK_EQ(mem.header(0x1020)->size, 992u);   // the remainder carries a free header
  CHECK_EQ(mem.header(0x1020)->id, 0u);
}

TEST("allocator.freelist_coalesces_neighbours") {
  Memory mem(0x1000, 1024);
  FreeListAllocator fl(mem, 0x1000, 0x1400);
  Addr a = fl.allocate(64), b = fl.allocate(64), c = fl.allocate(64);
  fl.release(a, 64);
  fl.release(c, 64);
  CHECK_EQ(fl.freeBlockCount(), 2u + 0u + 0u);  // hole at a, and (c merged with the tail)
  fl.release(b, 64);                            // fills the middle: everything merges
  CHECK_EQ(fl.freeBlockCount(), 1u);
  CHECK_EQ(fl.largestFree(), 1024u);
  CHECK(fl.coalesces() >= 3u);
}

TEST("allocator.freelist_first_fit_reuses_lowest_hole") {
  Memory mem(0x1000, 1024);
  FreeListAllocator fl(mem, 0x1000, 0x1400);
  Addr a = fl.allocate(64), b = fl.allocate(64), c = fl.allocate(64);
  (void)b;
  fl.release(a, 64);
  fl.release(c, 64);
  CHECK_EQ(fl.allocate(32), a);   // first fit: the lowest address wins
}

TEST("allocator.freelist_slack_is_not_lost") {
  Memory mem(0x1000, 1024);
  FreeListAllocator fl(mem, 0x1000, 0x1400);
  uint32_t actual = 0;
  Addr a = fl.allocate(1024 - 8);   // would leave an 8-byte remainder, still splittable
  (void)a;
  fl.reset();
  Addr x = fl.allocateBlock(1016, actual);
  CHECK(x != 0);
  CHECK(actual == 1016u || actual == 1024u);
  CHECK_EQ(fl.freeBytes() + actual, 1024u);
}

TEST("allocator.fragmentation_is_tracked") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (int i = 1; i <= 8; ++i) node(rt, "o" + std::to_string(i));
  for (int i : {2, 4, 6, 8}) rt.addRoot("r" + std::to_string(i), rt.resolve("o" + std::to_string(i)));
  rt.collect();
  RegionStats s = rt.heap.stats();
  CHECK_EQ(s.objects, 4u);
  CHECK_EQ(s.freeBlocks, 5u);                  // four holes + the tail
  CHECK(s.fragmentation > 0.0);
  CHECK(s.largestFree < s.free);
  CHECK_CONSISTENT(rt);
}

TEST("allocator.reuses_swept_hole") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  node(rt, "A"); node(rt, "B"); node(rt, "C");
  rt.addRoot("rA", rt.resolve("A"));
  rt.addRoot("rC", rt.resolve("C"));
  Addr bAddr = addr(rt, "B");
  rt.collect();
  CHECK(!alive(rt, "B"));
  ObjId d = node(rt, "D");
  CHECK_EQ(rt.addrOf(d), bAddr);   // the new object consumed the freed hole
}

// ------------------------------------------------------------ object model
TEST("objects.header_layout") {
  CHECK_EQ(sizeof(ObjectHeader), 24u);
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = rt.allocate("Node", 2, 0, "A");
  ObjectHeader& h = rt.header(a);
  CHECK_EQ(h.id, a);
  CHECK_EQ(h.size, 32u);          // 24 header + 2*4 refs
  CHECK_EQ(h.numRefs, 2u);
  CHECK_EQ(rt.typeName(h.type), std::string("Node"));
  CHECK_EQ(h.age, 0u);
  CHECK_EQ(rt.heap.mem().refs(rt.addrOf(a))[0], 0u);
}

TEST("objects.size_includes_payload_and_alignment") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = rt.allocate("Blob", 0, 10, "A");
  CHECK_EQ(rt.header(a).size, 40u);        // 24 + 10 -> 34 -> 40
  ObjId b = rt.allocate("Pair", 1, 0, "B");
  CHECK_EQ(rt.header(b).size, 32u);        // 24 + 4 -> 28 -> 32
}

TEST("objects.ids_are_deterministic_and_sequential") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  CHECK_EQ(node(rt, "A"), 1u);
  CHECK_EQ(node(rt, "B"), 2u);
  CHECK_EQ(node(rt, "C"), 3u);
  rt.reset();
  CHECK_EQ(node(rt, "A"), 1u);   // reset restarts numbering
}

TEST("objects.references_store_target_addresses") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "A"), b = node(rt, "B");
  CHECK(rt.setRef(a, 1, b));
  CHECK_EQ(rawTarget(rt, "A", 1), rt.addrOf(b));
  CHECK_EQ(target(rt, "A", 1), b);
  CHECK(rt.setRef(a, 1, 0));
  CHECK_EQ(rawTarget(rt, "A", 1), 0u);
  CHECK(!rt.setRef(a, 2, b));       // no such slot
  CHECK(!rt.setRef(a, 0, 99));      // no such object
}

TEST("objects.labels_resolve") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  ObjId a = node(rt, "");           // auto label
  ObjId b = node(rt, "");
  CHECK_EQ(rt.label(a), std::string("A"));
  CHECK_EQ(rt.label(b), std::string("B"));
  CHECK_EQ(rt.resolve("B"), b);
  CHECK_EQ(rt.resolve("#2"), b);
  CHECK_EQ(rt.resolve("2"), b);
  CHECK_EQ(rt.resolve("nope"), 0u);
  CHECK_EQ(rt.name(a), std::string("A (#1)"));
}

TEST("objects.out_of_memory_without_auto_gc") {
  Config c = cfgFor(Algo::MarkSweep);
  c.autoGC = false;
  Runtime rt(c);
  int made = 0;
  while (rt.allocate("Node", 2, 0, "") != 0) ++made;
  CHECK_EQ(made, 64);               // 2048 / 32
  CHECK(count(rt, EventType::OutOfMemory) == 1);
}

TEST("objects.auto_gc_recovers_from_exhaustion") {
  Runtime rt(cfgFor(Algo::MarkSweep));
  for (int i = 0; i < 200; ++i) CHECK(rt.allocate("Node", 2, 0, "") != 0);   // all garbage: GC keeps making room
  CHECK(rt.metrics.collections >= 1);
  CHECK_CONSISTENT(rt);
}
