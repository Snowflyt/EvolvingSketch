#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <utility>

#ifndef NDEBUG
#include <stdexcept>
#endif

#include <magic_enum/magic_enum.hpp>

#include "list.hpp"

#ifndef NDEBUG
#include "debug.hpp"
#endif

/**
 * @brief A FIFO (First-In-First-Out) ring buffer with a fixed capacity.
 *
 * Supports the following operations:
 * - Enqueue an element
 * - Dequeue the oldest element
 * - Get the current size of the FIFO
 * - Check if the FIFO is empty or full
 */
template <typename T> class RingBufferFIFO {
private:
public:
  explicit RingBufferFIFO(size_t capacity) : k_capacity_(capacity), buffer_(new T[capacity]) {}

  ~RingBufferFIFO() { delete[] buffer_; }

  // Copy constructor
  RingBufferFIFO(const RingBufferFIFO &other)
      : k_capacity_(other.k_capacity_), buffer_(new T[other.k_capacity_]), head_(other.head_),
        tail_(other.tail_), size_(other.size_) {
    std::copy(other.buffer_, other.buffer_ + other.k_capacity_, buffer_);
  }

  // Copy assignment operator
  auto operator=(const RingBufferFIFO &other) -> RingBufferFIFO & {
    if (this != &other) {
      delete[] buffer_;
      k_capacity_ = other.k_capacity_;
      buffer_ = new T[other.k_capacity_];
      head_ = other.head_;
      tail_ = other.tail_;
      size_ = other.size_;
      std::copy(other.buffer_, other.buffer_ + other.k_capacity_, buffer_);
    }
    return *this;
  }

  // Move constructor
  RingBufferFIFO(RingBufferFIFO &&other) noexcept
      : k_capacity_(other.k_capacity_), buffer_(other.buffer_), head_(other.head_),
        tail_(other.tail_), size_(other.size_) {
    other.buffer_ = nullptr;
    other.size_ = 0;
  }

  // Move assignment operator
  auto operator=(RingBufferFIFO &&other) noexcept -> RingBufferFIFO & {
    if (this != &other) {
      delete[] buffer_;
      k_capacity_ = other.k_capacity_;
      buffer_ = other.buffer_;
      head_ = other.head_;
      tail_ = other.tail_;
      size_ = other.size_;
      other.buffer_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  /**
   * @brief Enqueue an element into the FIFO.
   */
  void enqueue(const T &element) {
    if (size_ == k_capacity_) {
      // FIFO is full, remove the oldest entry
      buffer_[head_] = element;
      head_ = (head_ + 1) % k_capacity_;
      size_--;
    }

    // Insert the new entry
    buffer_[tail_] = element;
    tail_ = (tail_ + 1) % k_capacity_;
    size_++;
  }

  // Dequeue the oldest key-value pair from the FIFO
  auto dequeue() -> T {
#ifndef NDEBUG
    if (size_ == 0)
      throw std::underflow_error("FIFO is empty");
#endif

    T result = std::move(buffer_[head_]);

    // Update head and size
    head_ = (head_ + 1) % k_capacity_;
    size_--;

    return result;
  }

  // Get the capacity of the FIFO
  [[nodiscard]] auto capacity() const -> size_t { return k_capacity_; }

  // Get the current size of the FIFO
  [[nodiscard]] auto size() const -> size_t { return size_; }

  // Check if the FIFO is empty
  [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

  // Check if the FIFO is full
  [[nodiscard]] auto full() const -> bool { return size_ == k_capacity_; }

private:
  size_t k_capacity_; // Maximum capacity of the FIFO

  T *buffer_;       // Dynamically allocated buffer for entries
  size_t head_ = 0; // Index of the oldest entry
  size_t tail_ = 0; // Index for the next insertion
  size_t size_ = 0; // Current size of the FIFO
};

/**
 * @brief A FIFO (First-In-First-Out) ring buffer with key-value pairs and a fixed capacity.
 *
 * In addition to a ring buffer for FIFO, this class also maintains an `std::unordered_map` for
 * finding the index in the Ring buffer corresponding to the key, so as to achieve O(1) lookup of
 * the value corresponding to the key.
 *
 * Supports the following operations:
 * - Enqueue a key-value pair
 * - Dequeue the oldest key-value pair
 * - Check if a key exists in the FIFO
 * - Get a mutable reference to the value associated with a key
 * - Get the current size of the FIFO
 * - Check if the FIFO is empty or full
 */
template <typename K, typename V> class MappedRingBufferFIFO {
public:
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<const K, V>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    // Constructor
    iterator(MappedRingBufferFIFO *parent, size_t index, size_t traversed)
        : parent_(parent), index_(index), traversed_(traversed) {}

    // Dereference operator
    auto operator*() -> reference {
      return *reinterpret_cast<std::pair<const K, V> *>(&parent_->buffer_[index_]);
    }

    // Arrow operator
    auto operator->() -> pointer {
      return reinterpret_cast<std::pair<const K, V> *>(&parent_->buffer_[index_]);
    }

    // Pre-increment operator
    auto operator++() -> iterator & {
#ifndef NDEBUG
      if (traversed_ >= parent_->size_)
        throw std::out_of_range("Iterator cannot be incremented past the end");
#endif

      index_ = (index_ + 1) % parent_->k_capacity_;
      traversed_++;
      return *this;
    }

    // Post-increment operator
    auto operator++(int) -> iterator {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    // Equality operator
    auto operator==(const iterator &other) const -> bool {
      return parent_ == other.parent_ && index_ == other.index_ && traversed_ == other.traversed_;
    }

    // Inequality operator
    auto operator!=(const iterator &other) const -> bool { return !(*this == other); }

  private:
    MappedRingBufferFIFO *parent_;
    size_t index_;
    size_t traversed_;
  };

  explicit MappedRingBufferFIFO(size_t capacity)
      : k_capacity_(capacity), buffer_(new std::pair<K, V>[capacity]) {}

  ~MappedRingBufferFIFO() { delete[] buffer_; }

  // Copy constructor
  MappedRingBufferFIFO(const MappedRingBufferFIFO &other)
      : k_capacity_(other.k_capacity_), buffer_(new std::pair<K, V>[other.k_capacity_]),
        head_(other.head_), tail_(other.tail_), size_(other.size_) {
    std::copy(other.buffer_, other.buffer_ + other.k_capacity_, buffer_);
    rebuild_map();
  }

  // Copy assignment operator
  auto operator=(const MappedRingBufferFIFO &other) -> MappedRingBufferFIFO & {
    if (this != &other) {
      delete[] buffer_;
      k_capacity_ = other.k_capacity_;
      buffer_ = new std::pair<K, V>[other.k_capacity_];
      head_ = other.head_;
      tail_ = other.tail_;
      size_ = other.size_;
      std::copy(other.buffer_, other.buffer_ + other.k_capacity_, buffer_);
      map_ = other.map_;
      rebuild_map();
    }
    return *this;
  }

  // Move constructor
  MappedRingBufferFIFO(MappedRingBufferFIFO &&other) noexcept
      : k_capacity_(other.k_capacity_), buffer_(other.buffer_), head_(other.head_),
        tail_(other.tail_), size_(other.size_), map_(std::move(other.map_)) {
    other.buffer_ = nullptr;
    other.size_ = 0;
  }

  // Move assignment operator
  auto operator=(MappedRingBufferFIFO &&other) noexcept -> MappedRingBufferFIFO & {
    if (this != &other) {
      delete[] buffer_;
      k_capacity_ = other.k_capacity_;
      buffer_ = other.buffer_;
      head_ = other.head_;
      tail_ = other.tail_;
      size_ = other.size_;
      map_ = std::move(other.map_);
      other.buffer_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  // Enqueue a key-value pair into the FIFO
  void enqueue(const K &key, const V &value) {
#ifndef NDEBUG
    if (const auto it = map_.find(key); it != map_.end())
      throw std::invalid_argument(std::format("Key {} already exists", show(key)));
#endif

    if (size_ == k_capacity_) {
      // FIFO is full, remove the oldest entry
      const K &old_key = buffer_[head_].first;
      map_.erase(old_key);
      head_ = (head_ + 1) % k_capacity_;
      size_--;
    }

    // Insert the new entry
    buffer_[tail_].first = key;
    buffer_[tail_].second = value;
    map_[key] = tail_;
    tail_ = (tail_ + 1) % k_capacity_;
    size_++;
  }

  // Dequeue the oldest key-value pair from the FIFO
  auto dequeue() -> std::pair<const K, V> {
#ifndef NDEBUG
    if (size_ == 0)
      throw std::underflow_error("FIFO is empty");
#endif

    std::pair<const K, V> result = std::move(buffer_[head_]);

    // Erase the key from the map
    map_.erase(buffer_[head_].first);

    // Update head and size
    head_ = (head_ + 1) % k_capacity_;
    size_--;

    return result;
  }

  // Check if a key exists in the FIFO
  [[nodiscard]] auto contains(const K &key) const -> bool { return map_.contains(key); }

  // Find a key and return an iterator to the element or end()
  auto find(const K &key) -> iterator {
    if (const auto it = map_.find(key); it != map_.end()) {
      size_t index = it->second;
      return iterator(this, index, 0);
    }
    return end();
  }

  // Begin iterator
  auto begin() -> iterator { return iterator(this, head_, 0); }
  auto rbegin() -> iterator { return iterator(this, tail_, size_); }

  // End iterator
  auto end() -> iterator { return iterator(this, tail_, size_); }
  auto rend() -> iterator { return iterator(this, head_, 0); }

  // Get the capacity of the FIFO
  [[nodiscard]] auto capacity() const -> size_t { return k_capacity_; }

  // Get the current size of the FIFO
  [[nodiscard]] auto size() const -> size_t { return size_; }

  // Check if the FIFO is empty
  [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

  // Check if the FIFO is full
  [[nodiscard]] auto full() const -> bool { return size_ == k_capacity_; }

private:
  size_t k_capacity_;                 // Maximum capacity of the FIFO
  std::pair<K, V> *buffer_;           // Dynamically allocated buffer for entries
  size_t head_ = 0;                   // Index of the oldest entry
  size_t tail_ = 0;                   // Index for the next insertion
  size_t size_ = 0;                   // Current size of the FIFO
  std::unordered_map<K, size_t> map_; // Map to store key to index mapping

  // Rebuild the map based on the current buffer state
  void rebuild_map() {
    map_.clear();
    for (size_t i = 0; i < size_; ++i) {
      size_t index = (head_ + i) % k_capacity_;
      map_[buffer_[index].first] = index;
    }
  }
};

/**
 * @brief A FIFO (First-In-First-Out) implemented as a doubly linked list with an upper bound on the
 * number of elements.
 *
 * Supports the following operations:
 * - Enqueue an element
 * - Dequeue the oldest element
 * - Get the current size of the FIFO
 * - Check if the FIFO is empty or full
 */
template <typename T> class DoublyLinkedListFIFO {
private:
public:
  explicit DoublyLinkedListFIFO(size_t capacity) : k_capacity_(capacity) {}

  /**
   * @brief Enqueue an element into the FIFO.
   */
  void enqueue(const T &element) {
    if (size_ == k_capacity_) {
      list_.remove_tail();
      size_--;
    }

    // Insert the new entry
    list_.insert(element);
    size_++;
  }

  // Dequeue the oldest key-value pair from the FIFO
  auto dequeue() -> T {
#ifndef NDEBUG
    if (size_ == 0)
      throw std::underflow_error("FIFO is empty");
#endif

    T result = std::move(list_.tail()->value);
    size_--;

    return result;
  }

  // Get the capacity of the FIFO
  [[nodiscard]] auto capacity() const -> size_t { return k_capacity_; }

  // Get the current size of the FIFO
  [[nodiscard]] auto size() const -> size_t { return size_; }

  // Check if the FIFO is empty
  [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

  // Check if the FIFO is full
  [[nodiscard]] auto full() const -> bool { return size_ == k_capacity_; }

private:
  size_t k_capacity_; // Maximum capacity of the FIFO

  DoublyLinkedList<T> list_; // The doubly linked list to store the elements
  size_t size_ = 0;          // Current size of the FIFO
};

/**
 * @brief A FIFO (First-In-First-Out) implemented as a doubly linked list with key-value pairs and
 * an upper bound on the number of elements.
 *
 * In addition to a doubly linked list for FIFO, this class also maintains an `std::unordered_map`
 * for finding the node in the doubly linked list corresponding to the key, so as to achieve O(1)
 * lookup and deletion of the key-value pair.
 *
 * Supports the following operations:
 * - Enqueue a key-value pair
 * - Dequeue the oldest key-value pair
 * - Check if a key exists in the FIFO
 * - Delete a key-value pair
 * - Get a mutable reference to the value associated with a key
 * - Get the current size of the FIFO
 * - Check if the FIFO is empty or full
 */
template <typename K, typename V> class MappedDoublyLinkedListFIFO {
public:
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<const K, V>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    // Constructor
    iterator(MappedDoublyLinkedListFIFO *parent, Node<std::pair<K, V>> *curr, size_t traversed)
        : parent_(parent), curr_(curr), traversed_(traversed) {}

    // Dereference operator
    auto operator*() -> reference {
      return *reinterpret_cast<std::pair<const K, V> *>(&curr_->value);
    }

    // Arrow operator
    auto operator->() -> pointer {
      return reinterpret_cast<std::pair<const K, V> *>(&curr_->value);
    }

    // Pre-increment operator
    auto operator++() -> iterator & {
#ifndef NDEBUG
      if (traversed_ >= parent_->size_)
        throw std::out_of_range("Iterator cannot be incremented past the end");
#endif

      curr_ = curr_->next;
      traversed_++;
      return *this;
    }

    // Post-increment operator
    auto operator++(int) -> iterator {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    // Equality operator
    auto operator==(const iterator &other) const -> bool {
      return parent_ == other.parent_ && curr_ == other.curr_ && traversed_ == other.traversed_;
    }

    // Inequality operator
    auto operator!=(const iterator &other) const -> bool { return !(*this == other); }

  private:
    MappedDoublyLinkedListFIFO *parent_;
    Node<std::pair<K, V>> *curr_;
    size_t traversed_;
  };

  explicit MappedDoublyLinkedListFIFO(size_t capacity) : k_capacity_(capacity) {}

  // Enqueue a key-value pair into the FIFO
  void enqueue(const K &key, const V &value) {
#ifndef NDEBUG
    if (const auto it = map_.find(key); it != map_.end())
      throw std::invalid_argument(std::format("Key {} already exists", show(key)));
#endif

    if (size_ == k_capacity_) {
      // FIFO is full, remove the oldest entry
      const K &old_key = list_.tail()->value.first;
      map_.erase(old_key);
      list_.remove_tail();
      size_--;
    }

    // Insert the new entry
    map_[key] = list_.insert({key, value});
    size_++;
  }

  // Dequeue the oldest key-value pair from the FIFO
  auto dequeue() -> std::pair<const K, V> {
#ifndef NDEBUG
    if (size_ == 0)
      throw std::underflow_error("FIFO is empty");
#endif

    std::pair<const K, V> result = std::move(list_.tail()->value);

    // Erase the key from the map
    map_.erase(list_.tail()->value.first);

    // Update list and size
    list_.remove_tail();
    size_--;

    return result;
  }

  // Check if a key exists in the FIFO
  [[nodiscard]] auto contains(const K &key) const -> bool { return map_.contains(key); }

  // Find a key and return an iterator to the element or end()
  auto find(const K &key) -> iterator {
    if (const auto it = map_.find(key); it != map_.end()) {
      Node<std::pair<K, V>> *node = it->second;
      return iterator(this, node, 0);
    }
    return end();
  }

  /**
   * @brief Try to delete a key-value pair from the FIFO.
   *
   * @param key The key to delete.
   * @return `true` if the key was found and deleted, `false` otherwise.
   */
  auto remove(const K &key) -> bool {
    if (const auto it = map_.find(key); it != map_.end()) {
      list_.remove_node(it->second);
      map_.erase(it);
      size_--;
      return true;
    }
    return false;
  }

  // Begin iterator
  auto begin() -> iterator { return iterator(this, list_.head(), 0); }
  auto rbegin() -> iterator { return iterator(this, list_.tail(), size_); }

  // End iterator
  auto end() -> iterator { return iterator(this, list_.tail(), size_); }
  auto rend() -> iterator { return iterator(this, list_.head(), 0); }

  // Get the capacity of the FIFO
  [[nodiscard]] auto capacity() const -> size_t { return k_capacity_; }

  // Get the current size of the FIFO
  [[nodiscard]] auto size() const -> size_t { return size_; }

  // Check if the FIFO is empty
  [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

  // Check if the FIFO is full
  [[nodiscard]] auto full() const -> bool { return size_ == k_capacity_; }

private:
  size_t k_capacity_;                                  // Maximum capacity of the FIFO
  DoublyLinkedList<std::pair<K, V>> list_;             // The list to store the key-value pairs
  size_t size_ = 0;                                    // Current size of the FIFO
  std::unordered_map<K, Node<std::pair<K, V>> *> map_; // Map to store key to index mapping

  // Rebuild the map based on the current buffer state
  void rebuild_map() {
    map_.clear();
    Node<std::pair<K, V>> *curr = list_.head();
    while (curr != nullptr) {
      map_[curr->value.first] = curr;
      curr = curr->next;
    }
  }
};

/**
 * @brief A mappable FIFO (First-In-First-Out) implemented as a doubly linked list with an upper
 * bound on the number of elements.
 *
 * In addition to a doubly linked list for FIFO, this class also maintains an `std::unordered_map`
 * for finding the node in the doubly linked list corresponding to the element, so as to achieve
 * O(1) lookup and deletion of the element.
 *
 * Supports the following operations:
 * - Enqueue an element
 * - Dequeue the oldest element
 * - Check if an element exists in the FIFO
 * - Delete an element
 * - Get an immutable reference to the element associated with a key
 * - Get the current size of the FIFO
 * - Check if the FIFO is empty or full
 */
template <typename T> class MappableDoublyLinkedListFIFO {
public:
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type *;
    using reference = const value_type &;

    // Constructor
    iterator(MappableDoublyLinkedListFIFO *parent, Node<T> *curr, size_t traversed)
        : parent_(parent), curr_(curr), traversed_(traversed) {}

    // Dereference operator
    auto operator*() -> reference { return *reinterpret_cast<const T *>(&curr_->value); }

    // Arrow operator
    auto operator->() -> pointer { return reinterpret_cast<const T *>(&curr_->value); }

    // Pre-increment operator
    auto operator++() -> iterator & {
#ifndef NDEBUG
      if (traversed_ >= parent_->size_)
        throw std::out_of_range("Iterator cannot be incremented past the end");
#endif

      curr_ = curr_->next;
      traversed_++;
      return *this;
    }

    // Post-increment operator
    auto operator++(int) -> iterator {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    // Equality operator
    auto operator==(const iterator &other) const -> bool {
      return parent_ == other.parent_ && curr_ == other.curr_ && traversed_ == other.traversed_;
    }

    // Inequality operator
    auto operator!=(const iterator &other) const -> bool { return !(*this == other); }

  private:
    MappableDoublyLinkedListFIFO *parent_;
    Node<T> *curr_;
    size_t traversed_;
  };

  explicit MappableDoublyLinkedListFIFO(size_t capacity) : k_capacity_(capacity) {}

  // Enqueue a key-value pair into the FIFO
  void enqueue(const T &element) {
#ifndef NDEBUG
    if (const auto it = map_.find(element); it != map_.end())
      throw std::invalid_argument(std::format("Element {} already exists", show(element)));
#endif

    if (size_ == k_capacity_) {
      // FIFO is full, remove the oldest entry
      const T &old_element = list_.tail()->value;
      map_.erase(old_element);
      list_.remove_tail();
      size_--;
    }

    // Insert the new entry
    map_[element] = list_.insert(element);
    size_++;
  }

  // Dequeue the oldest key-value pair from the FIFO
  auto dequeue() -> T {
#ifndef NDEBUG
    if (size_ == 0)
      throw std::underflow_error("FIFO is empty");
#endif

    T result = std::move(list_.tail()->value);

    // Erase the element from the map
    map_.erase(list_.tail()->value);

    // Update list and size
    list_.remove_tail();
    size_--;

    return result;
  }

  // Check if an element exists in the FIFO
  [[nodiscard]] auto contains(const T &element) const -> bool { return map_.contains(element); }

  // Find an element and return an iterator to the element or end()
  auto find(const T &element) -> iterator {
    if (const auto it = map_.find(element); it != map_.end()) {
      Node<T> *node = it->second;
      return iterator(this, node, 0);
    }
    return end();
  }

  /**
   * @brief Try to delete an element from the FIFO.
   *
   * @param element The element to delete.
   * @return `true` if the element was found and deleted, `false` otherwise.
   */
  auto remove(const T &element) -> bool {
    if (const auto it = map_.find(element); it != map_.end()) {
      list_.remove_node(it->second);
      map_.erase(it);
      size_--;
      return true;
    }
    return false;
  }

  // Begin iterator
  auto begin() -> iterator { return iterator(this, list_.head(), 0); }
  auto rbegin() -> iterator { return iterator(this, list_.tail(), size_); }

  // End iterator
  auto end() -> iterator { return iterator(this, list_.tail(), size_); }
  auto rend() -> iterator { return iterator(this, list_.head(), 0); }

  // Get the capacity of the FIFO
  [[nodiscard]] auto capacity() const -> size_t { return k_capacity_; }

  // Get the current size of the FIFO
  [[nodiscard]] auto size() const -> size_t { return size_; }

  // Check if the FIFO is empty
  [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

  // Check if the FIFO is full
  [[nodiscard]] auto full() const -> bool { return size_ == k_capacity_; }

private:
  size_t k_capacity_;                    // Maximum capacity of the FIFO
  DoublyLinkedList<T> list_;             // The list to store the key-value pairs
  size_t size_ = 0;                      // Current size of the FIFO
  std::unordered_map<T, Node<T> *> map_; // Map to store key to index mapping

  // Rebuild the map based on the current buffer state
  void rebuild_map() {
    map_.clear();
    Node<T> *curr = list_.head();
    while (curr != nullptr) {
      map_[curr->value.first] = curr;
      curr = curr->next;
    }
  }
};
