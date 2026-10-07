// The managed object model: every object is a header followed by reference
// fields followed by opaque payload bytes.
//
//   +--------------------+  <- object address
//   | ObjectHeader (24B) |
//   +--------------------+
//   | Addr refs[numRefs] |  4 bytes each, 0 = null
//   +--------------------+
//   | payload bytes      |
//   +--------------------+  (total size rounded up to 8)
//
// A free block uses the same header with id == 0, which lets us walk a region
// linearly, object after object, even when it contains holes.
#pragma once
#include "minigc/common.h"

namespace minigc {

// Tri-colour marking states (see docs/mark-sweep.md).
enum Color : uint8_t { kWhite = 0, kGray = 1, kBlack = 2 };

struct ObjectHeader {
  uint32_t id;        // object id; 0 means "this block is free"
  uint32_t size;      // total block size in bytes, header included, multiple of 8
  uint32_t forward;   // forwarding address while an object is being relocated (0 = none)
  uint32_t refCount;  // used by the reference-counting collector
  uint16_t type;      // index into the runtime's type table
  uint8_t color;      // Color
  uint8_t age;        // number of minor GCs survived (generational)
  uint8_t numRefs;    // number of reference fields following the header
  uint8_t flags;      // reserved
  uint16_t pad;
};
static_assert(sizeof(ObjectHeader) == 24, "ObjectHeader must be 24 bytes");

constexpr uint32_t kHeaderSize = sizeof(ObjectHeader);
// A free block only needs `id` and `size` (the first 8 bytes of the header).
constexpr uint32_t kMinBlock = 8;
constexpr uint32_t kMaxRefs = 16;

inline uint32_t objectSize(uint32_t numRefs, uint32_t dataBytes) {
  return alignUp(kHeaderSize + 4 * numRefs + dataBytes);
}

}  // namespace minigc
