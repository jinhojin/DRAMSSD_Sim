#include "DRAMCache.h"
#include "Trace.h"

void DRAMCache::remove(const std::string &key) {
  if (auto it = keyToLru.find(key); it != std::end(keyToLru)) {
    assert(it->second->key == key);
    lru.erase(it->second);
    keyToLru.erase(it);

    freeCapacity += it->second->size;
  }
}

std::vector<DRAMCache::Item> DRAMCache::insert(const std::string &key,
                                               uint32_t size, bool isInFifo) {
  std::vector<DRAMCache::Item> victims;
  while (freeCapacity < size) {
    const auto &victim = lru.back();

    freeCapacity += victim.size;
    keyToLru.erase(victim.key);

    victims.push_back(victim);
    lru.pop_back();
  }

  lru.push_front({key, size, 0, isInFifo});
  keyToLru[key] = std::begin(lru);
  assert(freeCapacity >= size);
  freeCapacity -= size;

  return victims;
}

std::optional<DRAMCache::Item> DRAMCache::lookup(const std::string &key) {
  stat.numDramAccesses++;

  if (auto it = keyToLru.find(key); it != std::end(keyToLru)) {
    stat.numDramHits++;

    assert(it->second->key == key);
    lru.splice(std::begin(lru), lru, it->second);
    assert(it->second == std::begin(lru));
    it->second->numAccesses++;

    return *(it->second);
  }
  return std::nullopt;
}
