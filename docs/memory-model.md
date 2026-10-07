# Memory model: heap, regions, allocators

Source: `include/minigc/heap.h`, `src/core/heap.cpp`.

## 1. Concept
The **managed heap** is one reserved range of bytes (default 2048 bytes at simulated address `0x1000`). It is split into
**regions**; each region is managed by an **allocator** that tracks which bytes are free.

```
0x1000                                                                  0x1800
+--------------------------------------------------------------------------+
|                              MANAGED HEAP                                |
+--------------------------------------------------------------------------+
```

## 2. Why it exists
Allocation speed and fragmentation are the allocator's trade-off; reclaiming memory is the collector's job. Keeping them
separate lets you see that *Mark-Sweep leaves holes because the allocator keeps free blocks in a list*, whereas *copying
collectors never fragment because their allocator is a bump pointer over a freshly packed space*.

## 3. Data structures
* `Memory` — the byte buffer (`uint64_t[]` so it is 8-byte aligned), with `header(addr)`, `refs(addr)`, `move()`, `zero()`.
* `Allocator` (interface) → `BumpAllocator`, `FreeListAllocator`.
* `Region { kind, name, start, end, allocator }` and `Heap` (address validation `isObject`, `regionOf`, statistics).
* **Free blocks carry a header** with `id == 0`; therefore any region can be walked block by block (`Heap::forEachBlock`).

## 4. Algorithms

### Bump pointer (Phase 2a)
```
allocate(n): n = alignUp(n, 8); if cursor + n > end → fail; a = cursor; cursor += n; return a
```
O(1), never fragments, cannot free single blocks.

### Free list with splitting and coalescing (Phase 2b)
```
free list = address-ordered map {start -> size}
allocate(n): first block with size >= n (first fit)
             remainder = size - n; if remainder >= 8: split (remainder becomes a new free block)
                                    else: hand out the whole block (slack stays with the object)
release(b):  insert, then merge with the block that ends at b.start and the one that starts at b.end  (coalescing)
rebuildFromHeaders(): after a sweep, re-derive the list by walking the region and merging adjacent id==0 blocks
```

### Fragmentation
`fragmentation = 1 − largestFreeBlock / totalFree` (0 % = one big block; high = many small holes).

## 5. Example
```
Before:  [A][FREE 32][B][FREE 960 …]
allocate 32 →
After:   [A][C][B][FREE 960 …]          (first fit reuses the lowest hole)
```
Run: `demo 3` shows holes created by a sweep; `heap` prints them.

## 6. Visualization
Heap view, one row per region: allocated blocks (label + state), hatched **FREE** blocks, per-region *used / free blocks /
fragmentation %*. Allocation consuming free space is visible after every `new`.

## 7. Tests
`heap.*` (bounds, address validation, statistics), `allocator.*` (bump alignment/exhaustion, split, coalesce, first fit,
fragmentation, hole reuse).

## 8. How to run it
```
minigc> set heap 1024      # resize (resets the runtime)
minigc> new Node 2 0 A
minigc> heap
```
