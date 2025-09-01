#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "../utils/list.hpp"
#include "policy.hpp"

enum class WTinyLFUNodeType : uint8_t { WINDOW, PROBATION, PROTECTED };

template <typename K> struct WTinyLFUNodeValue {
  WTinyLFUNodeType type;
  K key;
};

// [ToS'17] TinyLFU: A Highly Efficient Cache Admission Policy
// * Link: https://dl.acm.org/doi/abs/10.1145/3149371
// * Paper: https://dl.acm.org/doi/pdf/10.1145/3149371
template <typename K, typename V, typename Sketch>
class WTinyLFUPolicy : public CacheReplacementPolicy<K, V> {
private:
  static constexpr double WINDOW_SIZE_RATIO = 0.01;
  static constexpr double PROBATION_SIZE_RATIO = 0.2;

public:
  explicit WTinyLFUPolicy(const size_t max_size, std::shared_ptr<Sketch> sketch)
      : k_max_window_size_(static_cast<size_t>(static_cast<double>(max_size) * WINDOW_SIZE_RATIO)),
        k_max_probation_size_((max_size - k_max_window_size_) * PROBATION_SIZE_RATIO),
        k_max_protected_size_(max_size - k_max_window_size_ - k_max_probation_size_),
        sketch_(sketch) {}

  void handle_cache_hit(const K &key) override {
    sketch_->update(key);

    auto *node = key2node_[key];

    switch (node->value.type) {
      using enum WTinyLFUNodeType;

    case WINDOW:
      window_list_.move_to_head(node);
      break;

    case PROBATION:
      probation_list_.transfer_node_to_head_of(node, protected_list_);
      node->value.type = PROTECTED;
      // If the protected list is full, evict the head of the probation list to the protected list
      if (protected_list_.size() - 1 == k_max_protected_size_) {
        auto *evicted = protected_list_.transfer_tail_to_head_of(probation_list_);
        evicted->value.type = PROBATION;
      }
      break;

    case PROTECTED:
      protected_list_.move_to_head(node);
      break;
    }
  }

  void handle_cache_miss(Cache<K, V> &cache, const K &key, const V &value) override {
    using enum WTinyLFUNodeType;

    sketch_->update(key);

    if (window_list_.size() == k_max_window_size_) {
      if (probation_list_.size() == k_max_probation_size_) {
        if (sketch_->estimate(window_list_.tail()->value.key) >
            sketch_->estimate(probation_list_.tail()->value.key)) {
          // Move window list tail to probation list and change its type
          auto *node = window_list_.transfer_tail_to_head_of(probation_list_);
          node->value.type = PROBATION;
          // Remove probation list tail to keep the size
          const K &evicted_key = probation_list_.tail()->value.key;
          key2node_.erase(evicted_key);
          cache.remove(evicted_key);
          probation_list_.remove_tail();
        } else {
          // Remove window list tail to keep the size
          const K &evicted_key = window_list_.tail()->value.key;
          key2node_.erase(evicted_key);
          cache.remove(evicted_key);
          window_list_.remove_tail();
        }
      } else {
        // If probation list is not full, move window tail to probation list and change its type
        auto *node = window_list_.transfer_tail_to_head_of(probation_list_);
        node->value.type = PROBATION;
      }
    }

    key2node_[key] = window_list_.insert({.type = WINDOW, .key = key});
    cache.put(key, value);
  }

  /* Benchmark start */
  [[nodiscard]] auto update_time_avg_seconds() const -> double {
    return sketch_->update_time_avg_seconds();
  }
  [[nodiscard]] auto estimate_time_avg_seconds() const -> double {
    return sketch_->estimate_time_avg_seconds();
  }
  /* Benchmark end */

private:
  size_t k_max_window_size_;
  size_t k_max_probation_size_;
  size_t k_max_protected_size_;

  DoublyLinkedList<WTinyLFUNodeValue<K>> window_list_;
  DoublyLinkedList<WTinyLFUNodeValue<K>> probation_list_;
  DoublyLinkedList<WTinyLFUNodeValue<K>> protected_list_;

  std::unordered_map<K, Node<WTinyLFUNodeValue<K>> *> key2node_;

  std::shared_ptr<Sketch> sketch_;
};
