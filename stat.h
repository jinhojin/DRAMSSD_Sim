#pragma once

#include <cstdint>

struct Stat {
  uint64_t numFifoAccesses{0};
  uint64_t numFifoHits{0};
  uint64_t numFifoOverWrittenHits{0};

  uint64_t numDramAccesses{0};
  uint64_t numDramHits{0};

  uint64_t numAccesses{0};
  uint64_t numHits{0};

  uint64_t numRemoved{0};

  Stat operator-(const Stat &stat) const {
    return {numFifoAccesses - stat.numFifoAccesses,
            numFifoHits - stat.numFifoHits,
            numFifoOverWrittenHits - stat.numFifoOverWrittenHits,
            numDramAccesses - stat.numDramAccesses,
            numDramHits - stat.numDramHits,
            numAccesses - stat.numAccesses,
            numHits - stat.numHits,
            numRemoved - stat.numRemoved};
  }
};
