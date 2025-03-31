#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

struct KeyValue {
    std::string key;
    std::string value;
    bool inSSD;
};

class Trace {
private:
    std::ifstream file_;

public:
    Trace(const std::string &filename) {
        file_.open(filename);
    }
    ~Trace() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    std::optional<KeyValue> get() {
        if (!file_.is_open() || file_.eof()) {
            return std::nullopt;
        }
        KeyValue kv;
        kv.inSSD = false; 

        if (file_ >> kv.key >> kv.value) {
            return kv;
        }
        return std::nullopt;
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
        return kv.key.size() + kv.value.size();
    }

    std::vector<KeyValue> put(const KeyValue &kv) {
        std::vector<KeyValue> evictedItems;

        auto it = std::find_if(vec_.begin(), vec_.end(),
            [&kv](const KeyValue &item){
                return item.key == kv.key;
            }
        );
        if (it != vec_.end()) {
            currentSize_ -= kvSize(*it);
            vec_.erase(it);
        }

        size_t newSize = kvSize(kv);

        if (newSize > capacity_) {
            std::cerr << "[Warning] Item exceeds DRAM capacity. Skipped.\n";
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

    std::optional<std::string> getValue(const std::string &key) {
        auto it = std::find_if(vec_.begin(), vec_.end(),
            [&key](const KeyValue &item){
                return item.key == key;
            }
        );
        if (it == vec_.end()) {
            return std::nullopt;
        }
        KeyValue found = *it;
        vec_.erase(it);
        vec_.insert(vec_.begin(), found);

        return found.value;
    }

    std::optional<KeyValue> remove(const std::string &key) {
        auto it = std::find_if(vec_.begin(), vec_.end(),
            [&key](const KeyValue &item){
                return item.key == key;
            }
        );
        if (it == vec_.end()) {
            return std::nullopt;
        }
        KeyValue temp = *it;
        currentSize_ -= kvSize(temp);
        vec_.erase(it);
        return temp;
    }
};


class Simulator {
private:
    LRUCache dram_;
    std::vector<KeyValue> ssd_; 
    size_t ssdCapacity_;        

public:
    Simulator(size_t dramSize, size_t ssdSize)
        : dram_(dramSize), ssdCapacity_(ssdSize) {}


    void processKeyValue(const KeyValue &kv) {
        std::vector<KeyValue> evicted = dram_.put(kv);

        for (auto &item : evicted) {
            item.inSSD = true;
            ssd_.push_back(item);
        }
    }

    std::optional<std::string> getValue(const std::string &key) {
        auto dramVal = dram_.getValue(key);
        if (dramVal.has_value()) {
            return dramVal;
        }

        auto ssdIt = std::find_if(ssd_.begin(), ssd_.end(),
            [&key](const KeyValue &item){
                return item.key == key;
            }
        );
        if (ssdIt == ssd_.end()) {
            return std::nullopt;
        }
        std::string val = ssdIt->value;

        KeyValue toPromote = *ssdIt;
        toPromote.inSSD = false; 

        std::vector<KeyValue> evictedFromDRAM = dram_.put(toPromote);

        for (auto &ev : evictedFromDRAM) {
            ev.inSSD = true;
            ssd_.push_back(ev);
        }
        ssd_.erase(ssdIt);

        return val;
    }

};

int main() {
    Simulator sim(50, 1000);

    Trace trace("test.txt");

    while (true) {
        auto kvOpt = trace.get();
        if (!kvOpt.has_value()) {
            std::cout << "EOF, quitting simulator.\n";
            break;
        }
        sim.processKeyValue(kvOpt.value());
    }


    return 0;
}
