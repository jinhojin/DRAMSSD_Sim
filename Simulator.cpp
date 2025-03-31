#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <unordered_map>
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
        std::string key = tokens[0];
        std::string op = tokens[1];
        int totalSize = std::stoi(tokens[2]);
        int keySize = std::stoi(tokens[4]);
        int metaSize = 0;
        int vSize = totalSize - keySize;
        KeyValue kv;
        kv.key = key;
        kv.valueSize = static_cast<size_t>(vSize);
        kv.metaSize = static_cast<size_t>(metaSize);
        kv.inSSD = false;
        return std::make_pair(op, kv);
    }
};

class LRUCache {
private:
    size_t capacity_;
    size_t currentSize_;
    std::vector<KeyValue> vec_;
public:
    LRUCache(size_t capacity)
        : capacity_(capacity), currentSize_(0) {
        vec_.reserve(1000);
    }
    size_t kvSize(const KeyValue &kv) const {
        return kv.key.size() + kv.valueSize + kv.metaSize;
    }
    std::optional<size_t> getValueSize(const std::string &key) {
        auto it = std::find_if(vec_.begin(), vec_.end(),
            [&key](const KeyValue &item){ return item.key == key; });
        if (it == vec_.end()) {
            return std::nullopt;
        }
        KeyValue found = *it;
        vec_.erase(it);
        vec_.insert(vec_.begin(), found);
        return found.valueSize;
    }
    std::vector<KeyValue> put(const KeyValue &kv) {
        std::vector<KeyValue> evictedItems;
        auto it = std::find_if(vec_.begin(), vec_.end(),
            [&kv](const KeyValue &item){ return item.key == kv.key; });
        if (it != vec_.end()) {
            currentSize_ -= kvSize(*it);
            vec_.erase(it);
        }
        size_t newSize = kvSize(kv);
        if (newSize > capacity_) {
            return evictedItems;
        }
        vec_.insert(vec_.begin(), kv);
        currentSize_ += newSize;
        while (currentSize_ > capacity_) {
            KeyValue &old = vec_.back();
            currentSize_ -= kvSize(old);
            evictedItems.push_back(old);
            vec_.pop_back();
        }
        return evictedItems;
    }
};

class SSD {
private:
    size_t segSize;
    size_t pageSize;
    size_t pagesPerSeg;
    size_t totalSeg;
    size_t totalPages;
    size_t writePtr;
    robin_hood::unordered_map<std::string, KeyAgg> mapKeyAgg;
    std::vector<std::string> pageOwner;
public:
    SSD(size_t capacity) {
        segSize = 256 * 1024;
        pageSize = 4 * 1024;
        pagesPerSeg = segSize / pageSize;
        totalSeg = capacity / segSize;
        if (totalSeg == 0) totalSeg = 1;
        totalPages = totalSeg * pagesPerSeg;
        writePtr = 0;
        pageOwner.resize(totalPages, "");
    }
    bool put(const KeyValue &kv) {
        if (kv.valueSize > pageSize) return false;
        size_t oldPage = writePtr;
        std::string oldKey = pageOwner[oldPage];
        if (!oldKey.empty()) {
            auto itOld = mapKeyAgg.find(oldKey);
            if (itOld != mapKeyAgg.end()) {
                mapKeyAgg.erase(itOld);
            }
        }
        pageOwner[oldPage] = kv.key;
        auto it = mapKeyAgg.find(kv.key);
        if (it != mapKeyAgg.end()) {
            it->second.pageID = oldPage;
            it->second.valueSize = kv.valueSize;
        } else {
            KeyAgg agg;
            agg.pageID = oldPage;
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
        size_t pg = it->second.pageID;
        if (pg < pageOwner.size() && pageOwner[pg] == key) {
            pageOwner[pg] = "";
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
        : dram_(dramSize), ssd_(ssdSize), totalGets(0), dramMiss(0) {}
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
        std::cout << "Object Access Counts:\n";
        for (auto &p : objectAccessCount) {
            std::cout << "  " << p.first << " : " << p.second << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <DRAM_SIZE> <SSD_SIZE> <TRACE_FILE>\n";
        return 1;
    }
    size_t dramSize = std::stoul(argv[1]);
    size_t ssdSize = std::stoul(argv[2]);
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
