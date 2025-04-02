#pragma once

#include "DRAMCache.h"
#include "fifo.h"

class Simulator {
public:
  Simulator(uint64_t ssdSize, const std::string &overwrittenLog,
            const std::string &overwrittenAccLog, uint64_t dramSize)
      : fifo_(stat_, ssdSize, overwrittenLog, overwrittenAccLog),
        dramCache_(stat_, dramSize) {}

  bool lookup(const std::string &key) {
    stat_.numAccesses++;

    if (auto item = dramCache_.lookup(key)) {
      stat_.numHits++;
      return true;
    }

    if (auto item = fifo_.lookup(key)) {
      stat_.numHits++;
      auto victimsFromDram = dramCache_.insert(key, item.value().size, true);

      for (const auto &victim : victimsFromDram) {
        if (!victim.isInFifo) {
          fifo_.insert(victim);
        }
      }
      return true;
    }

    return false;
  }

  void insert(const std::string &key, uint32_t size) {
    auto victimsFromDram = dramCache_.insert(key, size, false);

    for (const auto &victim : victimsFromDram) {
      if (!victim.isInFifo) {
        fifo_.insert(victim);
      }
    }
  }

  void remove(const std::string &key) {
    stat_.numRemoved++;

    dramCache_.remove(key);
    fifo_.remove(key);
  }

  const Stat& getStat() const { return stat_; }

private:
  Stat stat_;
  Fifo fifo_;
  DRAMCache dramCache_;
};
