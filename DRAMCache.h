#pragma once

#include "include/fmt/core.h"
#include "include/robin_hood.h"
#include "stat.h"
#include <cstdint>
#include <iostream>
#include <list>
#include <optional>

class DRAMCache {
  struct Item {
    std::string key;
    uint32_t size;
    uint32_t numAccesses;
    bool isInFifo;
  };

public:
  DRAMCache(Stat &stat, uint64_t capacity)
      : stat(stat), capacity(capacity), freeCapacity(capacity) {
    std::cout << fmt::format("DRAM size: {:.2f} MB",
                             static_cast<double>(capacity) / std::pow(1024, 2))
              << std::endl;
  }

  void remove(const std::string &key);

  std::vector<Item> insert(const std::string &key, uint32_t size,
                           bool isInFifo);

  std::optional<DRAMCache::Item> lookup(const std::string &key);

private:
  Stat &stat;
  const uint64_t capacity;
  uint64_t freeCapacity;

  // front: recently accessed items
  // back: least recently used
  std::list<Item> lru;

  robin_hood::unordered_map<std::string, std::list<Item>::iterator> keyToLru;
};
