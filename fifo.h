#pragma once

#include "DRAMCache.h"
#include "stat.h"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <vector>

#include "include/fmt/core.h"
#include "include/robin_hood.h"

class Fifo {
public:
  struct Item {
    static constexpr uint32_t kMetadataSize = 20;
    std::string key;
    uint32_t size{0};
    uint32_t numAccesses{0};
    uint32_t segId{0};
    uint32_t rotationCounter{0};
    bool isErased{false};

    uint32_t getSize() const { return size + kMetadataSize; }
  };

private:
  class Page {
  public:
    static constexpr uint32_t kPageSize = 4096;

    Page(uint32_t segId, uint32_t pageId)
        : segId(segId), pageId(pageId), freeCapacity(kPageSize) {}

    bool isFull(uint32_t size) const {
      return freeCapacity < size + Fifo::Item::kMetadataSize;
    }

    uint32_t insert(const std::string &key, uint32_t size) {
      assert(freeCapacity >= size + Fifo::Item::kMetadataSize);
      freeCapacity -= (size + Fifo::Item::kMetadataSize);
      items[key] = {.key = key,
                    .size = size,
                    .numAccesses = 0,
                    .segId = segId,
                    .rotationCounter = 0,
                    .isErased = false};
      return pageId;
    }

    std::optional<Fifo::Item> lookup(const std::string &key) {
      auto it = items.find(key);
      // TODO: it is guaranteed that item is in the page.
      if (it != std::end(items)) {
        it->second.numAccesses++;
        return std::make_optional(it->second);
      }
      return std::nullopt;
    }

    void remove(const std::string &key) {
      auto it = items.find(key);
      if (it != std::end(items)) {
        it->second.isErased = true;
      }
    }

    void clear(std::vector<Fifo::Item> &victims) {
      freeCapacity = kPageSize;

      for (const auto &[key, item] : items) {
        victims.push_back(item);
      }
      items.clear();
    }

    uint32_t getNumItems() const { return items.size(); }

  private:
    const uint32_t segId;
    const uint32_t pageId;
    uint32_t freeCapacity;

    // This could be duplicated.
    // To avoid duplication, need to manage hashmap in FIFO (i.e., key to item)
    robin_hood::unordered_map<std::string, Fifo::Item> items;
  };

  class Segment {
  public:
    static constexpr uint32_t kSegmentSize = 256 * 1024;

    Segment(uint32_t segId) : segId_(segId), pageIdx_(0) {
      const uint32_t numPagesPerSegment = kSegmentSize / Page::kPageSize;
      uint32_t startPageId = segId * numPagesPerSegment;
      uint32_t endPageId = (segId + 1) * numPagesPerSegment;
      for (uint32_t pageId = startPageId; pageId < endPageId; ++pageId) {
        pages_.push_back({segId, pageId});
      }
    }

    bool isFull(uint32_t size) const {
      return (pageIdx_ == pages_.size()) ||
             (pageIdx_ == pages_.size() - 1 && pages_[pageIdx_].isFull(size));
    }

    uint32_t insert(const std::string &key, uint32_t size) {
      assert(pageIdx_ < pages_.size());
      if (pages_[pageIdx_].isFull(size)) {
        pageIdx_++;
      }
      return pages_[pageIdx_].insert(key, size);
    }

    std::optional<Fifo::Item> lookup(const std::string &key, uint32_t pageId) {
      uint32_t targetPageIdx = pageId % (kSegmentSize / Page::kPageSize);
      return pages_[targetPageIdx].lookup(key);
    }

    std::vector<Fifo::Item> clear() {
      uint32_t numVictims = std::accumulate(
          std::begin(pages_), std::end(pages_), static_cast<uint32_t>(0),
          [](uint32_t acc, const auto &page) {
            return acc + page.getNumItems();
          });
      if (numVictims == 0) {
        assert(pageIdx_ == 0);
        return {};
      }

      std::vector<Fifo::Item> victims;
      victims.reserve(numVictims);
      for (uint32_t i = 0; i < pages_.size(); ++i) {
        pages_[i].clear(victims);
      }

      pageIdx_ = 0;
      return victims;
    }

    void remove(const std::string &key, uint32_t pageId) {
      uint32_t targetPageIdx = pageId % (kSegmentSize / Page::kPageSize);
      assert(targetPageIdx < pages_.size());
      return pages_[targetPageIdx].remove(key);
    }

  private:
    const uint32_t segId_;
    uint32_t pageIdx_;
    std::vector<Page> pages_;
  };

public:
  Fifo(Stat &stat, uint64_t capacity, const std::string &overwrittenLogFile,
       const std::string &overwrittenAccessedLogFile)
      : stat(stat), numTotalSegments(capacity / Segment::kSegmentSize),
        curSegmentPtr(0), rotationCounter(0) {
    for (uint32_t i = 0; i < numTotalSegments; ++i) {
      segments.push_back(i);
    }
    overwrittenLogFile_.open(overwrittenLogFile,
                             std::ios::out | std::ios::trunc);
    if (!overwrittenLogFile_.is_open()) {
      throw std::runtime_error("Failed to open file: " + overwrittenLogFile);
    }

    overwrittenAccessedLogFile_.open(overwrittenAccessedLogFile,
                                     std::ios::out | std::ios::trunc);
    if (!overwrittenAccessedLogFile_.is_open()) {
      throw std::runtime_error("Failed to open file: " + overwrittenLogFile);
    }
  }

  std::vector<Fifo::Item> insert(const DRAMCache::Item &dramItem);

  std::optional<Fifo::Item> lookup(const std::string &key);

  void remove(const std::string &key);

private:
  Stat &stat;
  const uint32_t numTotalSegments;
  const uint32_t numPagesPerSegment = Segment::kSegmentSize / Page::kPageSize;
  // const uint32_t reinsertionThreshold;
  // const uint32_t cleanThreshold;

  std::vector<Segment> segments;
  uint64_t curSegmentPtr;

  uint64_t rotationCounter;

  std::ofstream overwrittenLogFile_;
  std::ofstream overwrittenAccessedLogFile_;

  // key to access counter
  robin_hood::unordered_map<std::string, uint32_t> keyToSegId;
  robin_hood::unordered_map<std::string, Item> overwrittenItems;

  // dram access count holder
  robin_hood::unordered_map<std::string, std::vector<uint32_t>>
      keyToDramAccessCounter;
  // flash access reuse distance
  robin_hood::unordered_map<std::string, std::vector<uint64_t>>
      keyToReuseDistance;

  uint64_t getGlobalSegmentPtr(uint64_t rotationCounter,
                               uint64_t localSegmentPtr) const {
    return rotationCounter * numTotalSegments + localSegmentPtr;
  }
};
