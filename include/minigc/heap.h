// The managed heap: a reserved byte range, carved into Regions, each owned by
// an Allocator. The heap knows nothing about garbage collection.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "minigc/common.h"
#include "minigc/object.h"

namespace minigc {

// ------------------------------------------------------------------ Memory
// The raw reserved bytes. Addresses are simulated: address `a` lives at
// buffer offset (a - base).
class Memory {
 public:
  Memory(Addr base, uint32_t size);
  Addr base() const { return base_; }
  Addr end() const { return end_; }
  uint32_t size() const { return end_ - base_; }
  bool contains(Addr a) const { return a >= base_ && a < end_; }

  uint8_t* raw(Addr a) { return bytes_ + (a - base_); }
  ObjectHeader* header(Addr a) { return reinterpret_cast<ObjectHeader*>(raw(a)); }
  Addr* refs(Addr a) { return reinterpret_cast<Addr*>(raw(a) + kHeaderSize); }

  void zero(Addr from, uint32_t n);
  void move(Addr dst, Addr src, uint32_t n);  // memmove semantics
  void writeFreeHeader(Addr a, uint32_t size);

 private:
  Addr base_, end_;
  std::unique_ptr<uint64_t[]> storage_;  // uint64_t => 8-byte aligned
  uint8_t* bytes_;
};

// --------------------------------------------------------------- Allocators
// An allocator manages the free space of ONE region. It is independent from
// the garbage collector: it only hands out and takes back blocks.
class Allocator {
 public:
  virtual ~Allocator() = default;

  // Returns the address of a block of at least `size` bytes (0 if none fits).
  // `actual` is the real block size (>= size when a tiny remainder could not
  // be split off).
  virtual Addr allocateBlock(uint32_t size, uint32_t& actual) = 0;
  Addr allocate(uint32_t size) { uint32_t actual; return allocateBlock(size, actual); }

  virtual void reset() = 0;               // whole region becomes free again
  virtual Addr walkLimit() const = 0;     // blocks exist in [start, walkLimit)
  virtual uint32_t freeBytes() const = 0;
  virtual uint32_t largestFree() const = 0;
  virtual uint32_t freeBlockCount() const = 0;
  virtual const char* kind() const = 0;

  Addr start() const { return start_; }
  Addr end() const { return end_; }

 protected:
  Allocator(Memory& m, Addr s, Addr e) : mem_(m), start_(s), end_(e) {}
  Memory& mem_;
  Addr start_, end_;
};

// Phase 2a: allocate by moving a cursor. Fast, never fragments, cannot free
// individual blocks.
class BumpAllocator : public Allocator {
 public:
  BumpAllocator(Memory& m, Addr s, Addr e) : Allocator(m, s, e), cursor_(s) {}
  Addr allocateBlock(uint32_t size, uint32_t& actual) override;
  void reset() override { cursor_ = start_; }
  Addr walkLimit() const override { return cursor_; }
  uint32_t freeBytes() const override { return end_ - cursor_; }
  uint32_t largestFree() const override { return end_ - cursor_; }
  uint32_t freeBlockCount() const override { return cursor_ < end_ ? 1 : 0; }
  const char* kind() const override { return "bump"; }
  Addr cursor() const { return cursor_; }

 private:
  Addr cursor_;
};

// Phase 2b: address-ordered first-fit free list with block splitting and
// coalescing. Free blocks carry a header with id == 0 so the region stays
// linearly walkable.
class FreeListAllocator : public Allocator {
 public:
  FreeListAllocator(Memory& m, Addr s, Addr e);
  Addr allocateBlock(uint32_t size, uint32_t& actual) override;
  void reset() override;
  Addr walkLimit() const override { return end_; }
  uint32_t freeBytes() const override;
  uint32_t largestFree() const override;
  uint32_t freeBlockCount() const override { return static_cast<uint32_t>(free_.size()); }
  const char* kind() const override { return "free-list"; }

  // Give a block back; merges with free neighbours (coalescing).
  void release(Addr a, uint32_t size);
  // Re-derive the free list from the block headers: every id==0 block is
  // free, adjacent free blocks are merged. Used after a sweep.
  void rebuildFromHeaders();
  // Everything in [start, liveEnd) is in use; [liveEnd, end) is one free block.
  void resetFreeTail(Addr liveEnd);

  const std::map<Addr, uint32_t>& freeBlocks() const { return free_; }
  uint32_t splits() const { return splits_; }
  uint32_t coalesces() const { return coalesces_; }

 private:
  std::map<Addr, uint32_t> free_;  // address -> size
  uint32_t splits_ = 0, coalesces_ = 0;
};

// ------------------------------------------------------------------ Regions
enum class RegionKind { Main, FromSpace, ToSpace, Eden, SurvivorA, SurvivorB, Old };

inline bool isYoung(RegionKind k) {
  return k == RegionKind::Eden || k == RegionKind::SurvivorA || k == RegionKind::SurvivorB;
}

const char* regionKindName(RegionKind k);

struct Region {
  RegionKind kind;
  std::string name;
  Addr start = 0, end = 0;
  std::unique_ptr<Allocator> alloc;
  bool contains(Addr a) const { return a >= start && a < end; }
  uint32_t size() const { return end - start; }
};

struct RegionStats {
  uint32_t size = 0, used = 0, free = 0;
  uint32_t objects = 0, freeBlocks = 0, largestFree = 0;
  double fragmentation = 0;  // 1 - largestFree/free (0 when there is no free space)
};

class Heap {
 public:
  Heap(Addr base, uint32_t size) : mem_(base, size) {}

  Memory& mem() { return mem_; }
  Addr base() const { return mem_.base(); }
  Addr end() const { return mem_.end(); }
  uint32_t size() const { return mem_.size(); }

  Region& addRegion(RegionKind kind, const std::string& name, Addr start, uint32_t size, bool bump);
  void clearRegions() { regions_.clear(); }
  std::vector<std::unique_ptr<Region>>& regions() { return regions_; }
  Region* regionOf(Addr a);
  Region* region(RegionKind k);

  // Address validation: inside the heap, 8-aligned, and holds a live object header.
  bool isObject(Addr a);

  // Visit every block (objects and free blocks) of a region in address order.
  // f(Addr, ObjectHeader&). The visitor must not change block sizes.
  template <class F>
  void forEachBlock(Region& r, F f) {
    Addr a = r.start, limit = r.alloc->walkLimit();
    while (a < limit) {
      ObjectHeader* h = mem_.header(a);
      if (h->size == 0) break;  // corrupted heap; never loop forever
      uint32_t sz = h->size;
      f(a, *h);
      a += sz;
    }
  }
  // Visit only blocks that hold objects (id != 0), live or dead.
  template <class F>
  void forEachObject(Region& r, F f) {
    forEachBlock(r, [&](Addr a, ObjectHeader& h) { if (h.id != 0) f(a, h); });
  }

  RegionStats regionStats(Region& r);
  RegionStats stats();  // sum over all regions

 private:
  Memory mem_;
  std::vector<std::unique_ptr<Region>> regions_;
};

}  // namespace minigc
