#include "fifo.h"
#include <iostream>

#define ASSERT_WITH_MSG(expr, msg)                                             \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cout << "Assertion failed: " << msg << std::endl;                   \
      assert(expr);                                                            \
    }                                                                          \
  } while (0)

std::vector<Fifo::Item> Fifo::insert(const DRAMCache::Item &dramItem) {
  std::vector<Item> victims;
  // This happens only when clear threshold is not 0.
  if (segments[curSegmentPtr].isFull(dramItem.size)) {
    curSegmentPtr = (curSegmentPtr + 1) % numTotalSegments;
    rotationCounter += (curSegmentPtr == 0);

    if (curSegmentPtr == 0) {
      std::cout << fmt::format("Rotation count increases") << std::endl;
    }

    victims = segments[curSegmentPtr].clear();
    for (auto &victim : victims) {
      victim.rotationCounter = rotationCounter - 1;
      overwrittenItems[victim.key] = victim;
      keyToSegId.erase(victim.key);
      ASSERT_WITH_MSG(victim.segId == curSegmentPtr,
                      fmt::format("{}, {}", victim.segId, curSegmentPtr));
      assert(keyToDramAccessCounter.contains(victim.key));
      assert(keyToReuseDistance.contains(victim.key));

      const auto &reuseHistory = keyToReuseDistance[victim.key];
      uint32_t reuseHistorySize = reuseHistory.size();
      uint32_t reuseDist = (reuseHistorySize == 1)
                               ? 0
                               : reuseHistory[reuseHistorySize - 1] -
                                     reuseHistory[reuseHistorySize - 2];

      overwrittenLogFile_ << fmt::format(
                                 "{} {} {} {}",
                                 getGlobalSegmentPtr(victim.rotationCounter,
                                                     curSegmentPtr),
                                 victim.numAccesses,
                                 keyToDramAccessCounter[victim.key][0],
                                 reuseDist)
                          << std::endl;
    }
  }

  keyToDramAccessCounter[dramItem.key].push_back(dramItem.numAccesses);
  keyToReuseDistance[dramItem.key].push_back(
      getGlobalSegmentPtr(rotationCounter, curSegmentPtr));

  ASSERT_WITH_MSG(curSegmentPtr < numTotalSegments,
                  fmt::format("{}, {}", curSegmentPtr, numTotalSegments));

  remove(dramItem.key);
  // Remove if key already exists
  uint32_t pageId = segments[curSegmentPtr].insert(dramItem.key, dramItem.size);
  keyToSegId[dramItem.key] = pageId;

  return victims;
}

std::optional<Fifo::Item> Fifo::lookup(const std::string &key) {
  stat.numFifoAccesses++;

  if (auto it = keyToSegId.find(key); it != std::end(keyToSegId)) {
    stat.numFifoHits++;

    uint32_t pageId = it->second;
    uint32_t segId = pageId / numPagesPerSegment;
    const auto item = segments[segId].lookup(key, pageId);
    assert(item.has_value());
    assert(keyToReuseDistance.contains(key));
    keyToReuseDistance[key].push_back(
        getGlobalSegmentPtr(rotationCounter, curSegmentPtr));
    return item;
  }

  // This part is used for analytics
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
