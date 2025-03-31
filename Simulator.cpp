#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include "include/robin_hood/robin_hood.h"

struct KeyValue {
    std::string key;
    size_t valueSize;
    size_t metaSize;
    bool inSSD;
};

struct KeyAgg {
    size_t pageID;    
    size_t valueSize; 
};

struct Page {
    size_t globalPageID; 
    std::string key;    
    size_t valueSize;    
};
struct Segment {
    std::vector<Page> pages;
};

class Trace {
private:
    std::ifstream file_;
public:
    Trace(const std::string &filename) {
        file_.open(filename);
        if (!file_.is_open()) {
            std::cerr << "Failed to open trace file: " << filename << "\n";
        }
    }
    ~Trace() {
        if (file_.is_open()) {
            file_.close();
        }
    }
    std::optional<std::pair<std::string, KeyValue>> get() {
        if (!file_.is_open() || file_.eof()) {
            return std::nullopt;
        }
        std::string line;
        if (!std::getline(file_, line)) {
            return std::nullopt;
        }
        std::stringstream ss(line);
        std::vector<std::string> tokens;
        {
            std::string token;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
        }
        if (tokens.size() < 5) {
            return std::nullopt;
        }
        std::string key     = tokens[0];
        std::string op      = tokens[1];
        int totalSize       = std::stoi(tokens[2]);
        int keySize         = std::stoi(tokens[4]);
        int metaSize        = 0;  
        int vSize           = totalSize - keySize;

        KeyValue kv;
        kv.key       = key;
        kv.valueSize = static_cast<size_t>(vSize);
        kv.metaSize  = static_cast<size_t>(metaSize);
        kv.inSSD     = false;
        return std::make_pair(op, kv);
    }
};

class LRUCache {
    struct Node {
        KeyValue kv;
    };

private:
    size_t capacity_;
    size_t currentSize_;

    std::list<Node> itemList;

    std::unordered_map<std::string, std::list<Node>::iterator> cacheMap;

    size_t kvSize(const KeyValue &kv) const {
        return kv.key.size() + kv.valueSize + kv.metaSize;
    }

    void moveToFront(std::list<Node>::iterator it) {
        itemList.splice(itemList.begin(), itemList, it);
    }

public:
    LRUCache(size_t capacity)
        : capacity_(capacity), currentSize_(0)
    {
    }

    std::optional<size_t> getValueSize(const std::string &key) {
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) {
            return std::nullopt;
        }
        auto nodeIt = it->second;
        moveToFront(nodeIt);

        return nodeIt->kv.valueSize;
    }

    std::vector<KeyValue> put(const KeyValue &kv) {
        std::vector<KeyValue> evictedItems;

        auto found = cacheMap.find(kv.key);
        if (found != cacheMap.end()) {
            auto nodeIt = found->second;
            currentSize_ -= kvSize(nodeIt->kv);
            itemList.erase(nodeIt);
            cacheMap.erase(found);
        }
        size_t needed = kvSize(kv);
        if (needed > capacity_) {
            return evictedItems;
        }

        Node newNode{kv};
        itemList.push_front(newNode);
        cacheMap[kv.key] = itemList.begin();
        currentSize_ += needed;

        while (currentSize_ > capacity_ && !itemList.empty()) {
            auto &oldNode = itemList.back();
            KeyValue &oldKV = oldNode.kv;
            size_t oldSize = kvSize(oldKV);
            currentSize_ -= oldSize;
            evictedItems.push_back(oldKV);

            cacheMap.erase(oldKV.key);
            itemList.pop_back();
        }

        return evictedItems;
    }
};

class SSD {
private:
    static constexpr size_t SEGMENT_SIZE = 256 * 1024;
    static constexpr size_t PAGE_SIZE    = 4 * 1024;

    std::vector<Segment> segments;

    robin_hood::unordered_map<std::string, KeyAgg> mapKeyAgg;

    size_t segCount;      
    size_t pagesPerSeg;   
    size_t totalPages;    
    size_t writePtr;      

public:
    SSD(size_t capacity) {
        pagesPerSeg = SEGMENT_SIZE / PAGE_SIZE;
        segCount = capacity / SEGMENT_SIZE;
        if (segCount == 0) {
            segCount = 1; 
        }
        totalPages = segCount * pagesPerSeg;

        segments.resize(segCount);
        for (size_t s = 0; s < segCount; s++) {
            segments[s].pages.resize(pagesPerSeg);
            for (size_t p = 0; p < pagesPerSeg; p++) {
                size_t globalID = s * pagesPerSeg + p;
                segments[s].pages[p].globalPageID = globalID;
                segments[s].pages[p].key = "";
                segments[s].pages[p].valueSize = 0;
            }
        }
        writePtr = 0;
    }

    bool put(const KeyValue &kv) {
        if (kv.valueSize > PAGE_SIZE) {
            return false; 
        }

        size_t globalID = writePtr;
        size_t segIndex = globalID / pagesPerSeg;
        size_t pageIndex = globalID % pagesPerSeg;

        Page &oldPage = segments[segIndex].pages[pageIndex];
        if (!oldPage.key.empty()) {
            auto it = mapKeyAgg.find(oldPage.key);
            if (it != mapKeyAgg.end()) {
                mapKeyAgg.erase(it);
            }
        }

        oldPage.key = kv.key;
        oldPage.valueSize = kv.valueSize;

        auto itKey = mapKeyAgg.find(kv.key);
        if (itKey != mapKeyAgg.end()) {
            itKey->second.pageID = globalID;
            itKey->second.valueSize = kv.valueSize;
        } else {
            KeyAgg agg;
            agg.pageID = globalID;
            agg.valueSize = kv.valueSize;
            mapKeyAgg.emplace(kv.key, agg);
        }

        writePtr = (writePtr + 1) % totalPages;

        return true;
    }

    std::optional<size_t> get(const std::string &key) {
        auto it = mapKeyAgg.find(key);
        if (it == mapKeyAgg.end()) {
            return std::nullopt;
        }
        return it->second.valueSize;
    }

    bool erase(const std::string &key) {
        auto it = mapKeyAgg.find(key);
        if (it == mapKeyAgg.end()) {
            return false;
        }
        size_t globalID = it->second.pageID;
        size_t segIndex = globalID / pagesPerSeg;
        size_t pageIndex = globalID % pagesPerSeg;

        if (segments[segIndex].pages[pageIndex].key == key) {
            segments[segIndex].pages[pageIndex].key.clear();
            segments[segIndex].pages[pageIndex].valueSize = 0;
        }

        mapKeyAgg.erase(it);
        return true;
    }
};

class Simulator {
private:
    LRUCache dram_;
    SSD ssd_;
    size_t totalGets;
    size_t dramMiss;
    std::unordered_map<std::string, size_t> objectAccessCount;

public:
    Simulator(size_t dramSize, size_t ssdSize)
        : dram_(dramSize), ssd_(ssdSize), totalGets(0), dramMiss(0)
    {}

    std::optional<size_t> getFromDRAMOrSSD(const KeyValue &kv) {
        totalGets++;
        objectAccessCount[kv.key]++;

        auto val = dram_.getValueSize(kv.key);
        if (val.has_value()) {
            return val;
        }

        dramMiss++;

        auto ssdVal = ssd_.get(kv.key);
        if (ssdVal.has_value()) {
            KeyValue promote = kv;
            promote.valueSize = ssdVal.value();

            auto evicted = dram_.put(promote);
            for (auto &e : evicted) {
                ssd_.put(e);
            }
            ssd_.erase(kv.key);

            return promote.valueSize;
        }
        auto evicted = dram_.put(kv);
        for (auto &e : evicted) {
            ssd_.put(e);
        }
        ssd_.put(kv);
        return kv.valueSize;
    }

    void printStats() {
        std::cout << "Total GETs: " << totalGets << "\n";
        if (totalGets > 0) {
            double ratio = static_cast<double>(dramMiss) / static_cast<double>(totalGets);
            std::cout << "DRAM miss ratio: " << ratio << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <DRAM_SIZE> <SSD_SIZE> <TRACE_FILE>\n";
        return 1;
    }
    size_t dramSize = std::stoul(argv[1]);
    size_t ssdSize  = std::stoul(argv[2]);
    std::string traceFile = argv[3];

    Simulator sim(dramSize, ssdSize);
    Trace trace(traceFile);

    while (true) {
        auto record = trace.get();
        if (!record.has_value()) {
            break;
        }
        auto [op, kv] = record.value();
        if (op == "GET") {
            sim.getFromDRAMOrSSD(kv);
        }
    }

    sim.printStats();
    return 0;
}
