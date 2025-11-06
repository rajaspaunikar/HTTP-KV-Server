#pragma once
#include <unordered_map>
#include <list>
#include <optional>
#include <mutex>

template<typename K, typename V>
class LRUCache {
    using ListIter = typename std::list<std::pair<K, V>>::iterator;

    std::list<std::pair<K, V>> dll;
    std::unordered_map<K, ListIter> map;
    size_t capacity;
    mutable std::mutex mtx;

public:
    explicit LRUCache(size_t cap) : capacity(cap) {}

    std::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = map.find(key);
        if (it == map.end()) return std::nullopt;

        dll.splice(dll.begin(), dll, it->second);
        return it->second->second;
    }

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = map.find(key);
        if (it != map.end()) {
            it->second->second = value;
            dll.splice(dll.begin(), dll, it->second);
            return;
        }

        if (dll.size() == capacity) {
            auto& back = dll.back();
            map.erase(back.first);
            dll.pop_back();
        }

        dll.emplace_front(key, value);
        map[key] = dll.begin();
    }

    void remove(const K& key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = map.find(key);
        if (it != map.end()) {
            dll.erase(it->second);
            map.erase(it);
        }
    }

    double hit_rate() const {
        std::lock_guard<std::mutex> lock(mtx);
        size_t hits = 0, total = 0;
        // Not tracking hits/misses here for simplicity
        return 0.0;
    }
};