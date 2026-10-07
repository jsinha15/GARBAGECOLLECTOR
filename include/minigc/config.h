// Runtime configuration. Every knob that changes behaviour lives here so that
// experiments are reproducible.
#pragma once
#include "minigc/common.h"

namespace minigc {

struct Config {
  Algo algo = Algo::MarkSweep;

  Addr heapBase = 0x1000;       // first heap address (so that 0 can mean null)
  uint32_t heapBytes = 2048;    // total managed heap size (multiple of 8)

  // Generational layout (fractions of heapBytes): eden 3/8, survivors 1/8 each, old 3/8.
  uint32_t promotionThreshold = 2;  // survive this many minor GCs, promote on the next
  uint32_t cardBytes = 128;         // size of one card in the simplified card table
  bool useCardTable = false;        // false: minor GC scans the remembered set; true: scans dirty cards

  bool autoGC = true;               // allocation failure triggers a collection
  bool useSafepoints = true;        // stop-the-world goes through the safepoint protocol
  uint32_t threads = 3;             // simulated application threads
};

}  // namespace minigc
