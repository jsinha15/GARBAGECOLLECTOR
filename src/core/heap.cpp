#include "minigc/heap.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace minigc {

// ------------------------------------------------------------------ Memory
Memory::Memory(Addr base, uint32_t size) : base_(base), end_(base + size) {
  if (size == 0 || size % kAlign != 0 || base % kAlign != 0)
    throw std::invalid_argument("heap base/size must be non-zero multiples of 8");
  storage_.reset(new uint64_t[size / 8]());  // zero-initialised
  bytes_ = reinterpret_cast<uint8_t*>(storage_.get());
}

void Memory::zero(Addr from, uint32_t n) { std::memset(raw(from), 0, n); }

void Memory::move(Addr dst, Addr src, uint32_t n) { std::memmove(raw(dst), raw(src), n); }

void Memory::writeFreeHeader(Addr a, uint32_t size) {
  std::memset(raw(a), 0, std::min<uint32_t>(size, kHeaderSize));
  ObjectHeader* h = header(a);
  h->id = 0;
  h->size = size;
}

// -------------------------------------------------------------- bump alloc
Addr BumpAllocator::allocateBlock(uint32_t size, uint32_t& actual) {
  size = alignUp(size);
  if (size > end_ - cursor_) return 0;
  Addr a = cursor_;
  cursor_ += size;
  actual = size;
  return a;
}

// --------------------------------------------------------- free-list alloc
FreeListAllocator::FreeListAllocator(Memory& m, Addr s, Addr e) : Allocator(m, s, e) { reset(); }

void FreeListAllocator::reset() {
  free_.clear();
  free_[start_] = end_ - start_;
  mem_.writeFreeHeader(start_, end_ - start_);
}

Addr FreeListAllocator::allocateBlock(uint32_t size, uint32_t& actual) {
  size = alignUp(size);
  for (auto it = free_.begin(); it != free_.end(); ++it) {  // first fit, address order
    if (it->second < size) continue;
    Addr a = it->first;
    uint32_t blockSize = it->second;
    free_.erase(it);
    uint32_t remainder = blockSize - size;
    if (remainder >= kMinBlock) {  // block splitting
      Addr rest = a + size;
      free_[rest] = remainder;
      mem_.writeFreeHeader(rest, remainder);
      actual = size;
      ++splits_;
    } else {
      actual = blockSize;  // too small to split off; caller keeps the slack
    }
    return a;
  }
  return 0;
}

uint32_t FreeListAllocator::freeBytes() const {
  uint32_t total = 0;
  for (const auto& kv : free_) total += kv.second;
  return total;
}

uint32_t FreeListAllocator::largestFree() const {
  uint32_t best = 0;
  for (const auto& kv : free_) best = std::max(best, kv.second);
  return best;
}

void FreeListAllocator::release(Addr a, uint32_t size) {
  // Mark the block itself free first: when it is merged into a neighbour its own
  // header stays behind inside the bigger block and must not look like a live object.
  mem_.writeFreeHeader(a, size);
  auto next = free_.lower_bound(a);
  // merge with the following free block?
  if (next != free_.end() && a + size == next->first) {
    size += next->second;
    next = free_.erase(next);
    ++coalesces_;
  }
  // merge with the preceding free block?
  if (next != free_.begin()) {
    auto prev = std::prev(next);
    if (prev->first + prev->second == a) {
      a = prev->first;
      size += prev->second;
      free_.erase(prev);
      ++coalesces_;
    }
  }
  free_[a] = size;
  mem_.writeFreeHeader(a, size);
}

void FreeListAllocator::rebuildFromHeaders() {
  free_.clear();
  Addr a = start_;
  Addr runStart = 0;
  uint32_t runSize = 0;
  auto flush = [&]() {
    if (runSize > 0) {
      free_[runStart] = runSize;
      mem_.writeFreeHeader(runStart, runSize);
    }
    runSize = 0;
  };
  while (a < end_) {
    ObjectHeader* h = mem_.header(a);
    if (h->size == 0) break;
    uint32_t sz = h->size;
    if (h->id == 0) {
      if (runSize == 0) runStart = a; else ++coalesces_;
      runSize += sz;
    } else {
      flush();
    }
    a += sz;
  }
  flush();
}

void FreeListAllocator::resetFreeTail(Addr liveEnd) {
  free_.clear();
  if (liveEnd < end_) {
    free_[liveEnd] = end_ - liveEnd;
    mem_.writeFreeHeader(liveEnd, end_ - liveEnd);
  }
}

// ------------------------------------------------------------------ Heap
const char* regionKindName(RegionKind k) {
  switch (k) {
    case RegionKind::Main: return "Main";
    case RegionKind::FromSpace: return "FromSpace";
    case RegionKind::ToSpace: return "ToSpace";
    case RegionKind::Eden: return "Eden";
    case RegionKind::SurvivorA: return "SurvivorA";
    case RegionKind::SurvivorB: return "SurvivorB";
    case RegionKind::Old: return "Old";
  }
  return "?";
}

Region& Heap::addRegion(RegionKind kind, const std::string& name, Addr start, uint32_t size, bool bump) {
  auto r = std::make_unique<Region>();
  r->kind = kind;
  r->name = name;
  r->start = start;
  r->end = start + size;
  if (bump) r->alloc = std::make_unique<BumpAllocator>(mem_, start, r->end);
  else r->alloc = std::make_unique<FreeListAllocator>(mem_, start, r->end);
  regions_.push_back(std::move(r));
  return *regions_.back();
}

Region* Heap::regionOf(Addr a) {
  for (auto& r : regions_)
    if (r->contains(a)) return r.get();
  return nullptr;
}

Region* Heap::region(RegionKind k) {
  for (auto& r : regions_)
    if (r->kind == k) return r.get();
  return nullptr;
}

bool Heap::isObject(Addr a) {
  if (!mem_.contains(a) || a % kAlign != 0) return false;
  if (a + kHeaderSize > mem_.end()) return false;
  ObjectHeader* h = mem_.header(a);
  return h->id != 0 && h->size >= kHeaderSize && a + h->size <= mem_.end();
}

RegionStats Heap::regionStats(Region& r) {
  RegionStats s;
  s.size = r.size();
  forEachObject(r, [&](Addr, ObjectHeader& h) { s.used += h.size; ++s.objects; });
  s.free = s.size - s.used;
  s.freeBlocks = r.alloc->freeBlockCount();
  s.largestFree = r.alloc->largestFree();
  s.fragmentation = s.free > 0 ? 1.0 - static_cast<double>(s.largestFree) / s.free : 0.0;
  return s;
}

RegionStats Heap::stats() {
  RegionStats total;
  for (auto& r : regions_) {
    RegionStats s = regionStats(*r);
    total.size += s.size;
    total.used += s.used;
    total.free += s.free;
    total.objects += s.objects;
    total.freeBlocks += s.freeBlocks;
    total.largestFree = std::max(total.largestFree, s.largestFree);
  }
  total.fragmentation = total.free > 0 ? 1.0 - static_cast<double>(total.largestFree) / total.free : 0.0;
  return total;
}

}  // namespace minigc
