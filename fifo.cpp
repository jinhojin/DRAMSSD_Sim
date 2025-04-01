#include "fifo.h"
#include <iostream>

std::vector<Fifo::Item> Fifo::insert(const std::string &key, uint32_t size) {
  // TODO: how to count rotation?
  std::vector<Item> victims;
  // This happens only when clear threshold is not 0.
  if (segments[curSegmentPtr].isFull(size)) {
    curSegmentPtr = (curSegmentPtr + 1) % numTotalSegments;
    rotationCounter += (curSegmentPtr == 0);

    if (curSegmentPtr == 0) {
      std::cout << fmt::format("Rotation count increases") << std::endl;
    }

    victims = segments[curSegmentPtr].clear();
    for (auto &victim : victims) {
      victim.rotationCounter = rotationCounter - 1;
      overwrittenItems[victim.key] = victim;

      overwrittenLogFile_ << fmt::format("{}", victim.numAccesses) << std::endl;
    }
  }

  assert(curSegmentPtr < numTotalSegments);

  // Remove if key already exists
  remove(key);
  uint32_t pageId = segments[curSegmentPtr].insert(key, size);
  keyToSegId[key] = pageId;

  return victims;
}

std::optional<Fifo::Item> Fifo::lookup(const std::string &key) {
  stat.numFifoAccesses++;

  if (auto it = keyToSegId.find(key); it != std::end(keyToSegId)) {
    stat.numFifoHits++;

    uint32_t pageId = it->second;
    uint32_t segId = pageId / numPagesPerSegment;
    return segments[segId].lookup(key, pageId);
  }

  if (auto it = overwrittenItems.find(key); it != std::end(overwrittenItems)) {
    stat.numFifoOverWrittenHits++;

    const uint32_t segDist =
        getGlobalSegmentPtr(rotationCounter, curSegmentPtr) -
        getGlobalSegmentPtr(it->second.rotationCounter, it->second.segId);
    const uint32_t numAccessesBefore = it->second.numAccesses;

    overwrittenItems.erase(it);

    overwrittenAccessedLogFile_
        << fmt::format("{} {}", segDist, numAccessesBefore) << std::endl;
  }

  return std::nullopt;
}

void Fifo::remove(const std::string &key) {
  if (auto it = keyToSegId.find(key); it != std::end(keyToSegId)) {
    uint32_t pageId = it->second;
    uint32_t segId = pageId / numPagesPerSegment;
    segments[segId].remove(key, pageId);
    keyToSegId.erase(it);
  }
}
