#pragma once

#include <cstddef>
#include <type_traits>
#include <unordered_set>

#include <spdlog/spdlog.h>

#ifndef NDEBUG
#include "../utils/debug.hpp"
#endif

template <typename K, typename V> class Cache {
public:
  using key_type = K;
  using value_type = V;

  virtual auto contains(const K &key) const -> bool = 0;

  virtual auto get(const K &key, V *value) const -> bool = 0;
  virtual void put(const K &key, const V &value) = 0;
  virtual void remove(const K &key) = 0;

  [[nodiscard]] virtual auto is_full() const -> bool = 0;
};

template <typename CACHE>
concept IsCache =
    std::is_base_of_v<Cache<typename CACHE::key_type, typename CACHE::value_type>, CACHE>;

template <typename K, typename V> class MockCache : public Cache<K, V> {
public:
  explicit MockCache(const size_t max_size) : k_max_size_(max_size) { keys_.reserve(max_size); }

  auto contains(const K &key) const -> bool override { return keys_.find(key) != keys_.end(); }

  auto get(const K &key, V * /*value*/) const -> bool override {
    return keys_.find(key) != keys_.end();
  }

  void put(const K &key, const V &value) override {
#ifndef NDEBUG
    if (keys_.size() >= k_max_size_ && !keys_.contains(key))
      spdlog::warn("MockCache: Suspicious insertion {} -> {} to a full cache ({} >= {})", show(key),
                   show(value), keys_.size(), k_max_size_);
#endif

    keys_.insert(key);
  }

  void remove(const K &key) override {
#ifndef NDEBUG
    if (!keys_.contains(key))
      spdlog::warn("MockCache: Suspicious removal of non-existing key {}", show(key));
#endif

    keys_.erase(key);
  }

  [[nodiscard]] auto is_full() const -> bool override { return keys_.size() == k_max_size_; }

private:
  size_t k_max_size_;

  std::unordered_set<K> keys_;
};

template <typename K, typename V> class Store {
public:
  virtual auto get(const K &key, V *value) const -> bool = 0;
  virtual void put(const K &key, const V &value) = 0;
  virtual auto remove(const K &key) -> bool = 0;
};

template <typename K, typename V> class MockStore : public Store<K, V> {
public:
  auto get(const K &key, V * /*value*/) const -> bool override {
    return keys_.find(key) != keys_.end();
  }

  void put(const K &key, const V & /*value*/) override { keys_.insert(key); }

  auto remove(const K &key) -> bool override { return keys_.erase(key) > 0; }

  static auto from_keys(const std::unordered_set<K> &keys) -> MockStore {
    MockStore store;
    store.keys_ = keys;
    return store;
  }

private:
  std::unordered_set<K> keys_;
};

template <typename K, typename V> class CacheReplacementPolicy {
public:
  explicit CacheReplacementPolicy() = default;
  virtual ~CacheReplacementPolicy() = default;

  CacheReplacementPolicy(const CacheReplacementPolicy &) = delete;
  CacheReplacementPolicy(CacheReplacementPolicy &&) = delete;
  auto operator=(const CacheReplacementPolicy &) -> CacheReplacementPolicy & = delete;
  auto operator=(CacheReplacementPolicy &&) -> CacheReplacementPolicy & = delete;

  virtual void handle_cache_hit(const K &key) = 0;
  virtual void handle_cache_miss(Cache<K, V> &cache, const K &key, const V &value) = 0;

  virtual void handle_update(const K &key, const V &value) {};
  virtual void handle_remove(const K &key) {};
};
