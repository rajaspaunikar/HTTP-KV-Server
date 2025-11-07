#pragma once

#include <iostream>
#include <string>
#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>

class LRUCache {
public:
    LRUCache(size_t capacity) : _capacity(capacity) {}

    // Get a value from the cache
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(_mutex);

        // Check if key exists in the map
        auto it = _map.find(key);
        if (it == _map.end()) {
            return std::nullopt; // Cache miss
        }

        // Key found: Move it to the front of the list (most recently used)
        _list.splice(_list.begin(), _list, it->second.second);
        
        // Return the value
        return it->second.first;
    }

    // Put a key-value pair into the cache
    void put(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(_mutex);

        // Check if key already exists
        auto it = _map.find(key);
        if (it != _map.end()) {
            // Key exists: update value and move to front
            it->second.first = value;
            _list.splice(_list.begin(), _list, it->second.second);
            return;
        }

        // Key doesn't exist: check for capacity
        if (_list.size() >= _capacity) {
            // Cache is full: evict the least recently used item (from the back)
            std::string lru_key = _list.back();
            _list.pop_back();
            _map.erase(lru_key);
        }

        // Add the new key-value pair to the front
        _list.push_front(key);
        _map[key] = {value, _list.begin()};
    }

    // Remove a key from the cache (for DELETE operations)
    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _map.find(key);
        if (it != _map.end()) {
            _list.erase(it->second.second);
            _map.erase(it);
        }
    }

private:
    size_t _capacity;
    std::list<std::string> _list; // Stores keys, front is MRU, back is LRU
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> _map; // key -> {value, list_iterator}
    std::mutex _mutex;
};