#pragma once

#include <cstddef>

#include "../utils/fifo.hpp"
#include "policy.hpp"

template <typename K, typename V> class FIFOPolicy : public CacheReplacementPolicy<K, V> {
public:
  explicit FIFOPolicy(const size_t max_size) : queue_(max_size) {}

  void handle_cache_hit(const K & /*key*/) override {
    // Do nothing
  }

  void handle_cache_miss(Cache<K, V> &cache, const K &key, const V &value) override {
    if (cache.is_full()) {
      const K evicted_key = queue_.dequeue();
      cache.remove(evicted_key);
    }

    cache.put(key, value);
    queue_.enqueue(key);
  }

private:
  RingBufferFIFO<K> queue_;
};
