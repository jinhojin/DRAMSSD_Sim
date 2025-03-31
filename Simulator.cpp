#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

struct KeyValue {
    std::string key;     
    size_t valueSize;    
    bool inSSD;          
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
            std::cerr << "[Warning] Invalid trace line: " << line << "\n";
            return std::nullopt;
        }

        std::string key    = tokens[0];
        std::string op     = tokens[1];
        int totalSize      = std::stoi(tokens[2]); 
        int keySize        = std::stoi(tokens[4]); 

        int vSize = totalSize - keySize;

        KeyValue kv;
        kv.key       = key;
        kv.valueSize = static_cast<size_t>(vSize);
        kv.inSSD     = false;

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
        : capacity_(capacity), currentSize_(0) 
    {
        vec_.reserve(1000);
    }

    size_t kvSize(const KeyValue &kv) const {
        return kv.key.size() + kv.valueSize;
    }

    std::optional<size_t> getValueSize(const std::string &key) {
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

        return found.valueSize;
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
            std::cerr << "[Warning] Item exceeds DRAM capacity. Skip.\n";
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

    std::vector<KeyValue> getAll() const {
        return vec_;
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


    std::optional<size_t> getFromDRAMOrSSD(const KeyValue &kv) {
        auto dramVal = dram_.getValueSize(kv.key);
        if (dramVal.has_value()) {
            return dramVal;
        }

        auto it = std::find_if(ssd_.begin(), ssd_.end(),
            [&kv](const KeyValue &item){ return item.key == kv.key; }
        );
        if (it != ssd_.end()) {
            KeyValue promote = *it;
            promote.inSSD = false;

            auto evicted = dram_.put(promote);
            for (auto &e : evicted) {
                e.inSSD = true;
                ssd_.push_back(e);
            }
            size_t valSz = it->valueSize;
            ssd_.erase(it);

            return valSz;
        }

        auto evicted = dram_.put(kv);
        for (auto &e : evicted) {
            e.inSSD = true;
            ssd_.push_back(e);
        }

        return kv.valueSize;
    }
};

int main() {
    Simulator sim(50, 1000);

    Trace trace("trace.csv");

    while (true) {
        auto record = trace.get();
        if (!record.has_value()) {
            std::cout << "EOF, quitting simulator.\n";
            break;
        }

        auto [op, kv] = record.value();

        if (op == "GET") {
            auto valSz = sim.getFromDRAMOrSSD(kv);
        } else {
            std::cout << "[Warning] Unhandled op: " << op << "\n";
        }
    }

    return 0;
}
