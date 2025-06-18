#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <stdexcept>

/**
 * @brief An array wrapper with a fixed capacity.
 */
template <typename T> class FixedSizeArray {
public:
  // Nested iterator class
  class iterator {
  public:
    // Iterator traits
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T *;
    using reference = T &;

    // Constructor
    iterator(pointer ptr) : ptr_(ptr) {}

    // Dereference operator
    reference operator*() const { return *ptr_; }

    // Arrow operator
    pointer operator->() const { return ptr_; }

    // Pre-increment
    iterator &operator++() {
      ++ptr_;
      return *this;
    }

    // Post-increment
    iterator operator++(int) {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    // Pre-decrement
    iterator &operator--() {
      --ptr_;
      return *this;
    }

    // Post-decrement
    iterator operator--(int) {
      iterator temp = *this;
      --(*this);
      return temp;
    }

    // Addition
    iterator operator+(difference_type offset) const { return iterator(ptr_ + offset); }

    // Subtraction
    iterator operator-(difference_type offset) const { return iterator(ptr_ - offset); }

    // Difference between iterators
    difference_type operator-(const iterator &other) const { return ptr_ - other.ptr_; }

    // Equality comparison
    bool operator==(const iterator &other) const { return ptr_ == other.ptr_; }

    // Inequality comparison
    bool operator!=(const iterator &other) const { return ptr_ != other.ptr_; }

    // Less than comparison
    bool operator<(const iterator &other) const { return ptr_ < other.ptr_; }

    // Greater than comparison
    bool operator>(const iterator &other) const { return ptr_ > other.ptr_; }

    // Less than or equal to comparison
    bool operator<=(const iterator &other) const { return ptr_ <= other.ptr_; }

    // Greater than or equal to comparison
    bool operator>=(const iterator &other) const { return ptr_ >= other.ptr_; }

    // Compound assignment operators
    iterator &operator+=(difference_type offset) {
      ptr_ += offset;
      return *this;
    }

    iterator &operator-=(difference_type offset) {
      ptr_ -= offset;
      return *this;
    }

  private:
    pointer ptr_;
  };

  // Nested const_iterator class
  class const_iterator {
  public:
    // Iterator traits
    using iterator_category = std::random_access_iterator_tag;
    using value_type = const T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T *;
    using reference = const T &;

    // Constructor
    const_iterator(pointer ptr) : ptr_(ptr) {}

    // Dereference operator
    reference operator*() const { return *ptr_; }

    // Arrow operator
    pointer operator->() const { return ptr_; }

    // Pre-increment
    const_iterator &operator++() {
      ++ptr_;
      return *this;
    }

    // Post-increment
    const_iterator operator++(int) {
      const_iterator temp = *this;
      ++(*this);
      return temp;
    }

    // Pre-decrement
    const_iterator &operator--() {
      --ptr_;
      return *this;
    }

    // Post-decrement
    const_iterator operator--(int) {
      const_iterator temp = *this;
      --(*this);
      return temp;
    }

    // Addition
    const_iterator operator+(difference_type offset) const { return const_iterator(ptr_ + offset); }

    // Subtraction
    const_iterator operator-(difference_type offset) const { return const_iterator(ptr_ - offset); }

    // Difference between iterators
    difference_type operator-(const const_iterator &other) const { return ptr_ - other.ptr_; }

    // Equality comparison
    bool operator==(const const_iterator &other) const { return ptr_ == other.ptr_; }

    // Inequality comparison
    bool operator!=(const const_iterator &other) const { return ptr_ != other.ptr_; }

    // Less than comparison
    bool operator<(const const_iterator &other) const { return ptr_ < other.ptr_; }

    // Greater than comparison
    bool operator>(const const_iterator &other) const { return ptr_ > other.ptr_; }

    // Less than or equal to comparison
    bool operator<=(const const_iterator &other) const { return ptr_ <= other.ptr_; }

    // Greater than or equal to comparison
    bool operator>=(const const_iterator &other) const { return ptr_ >= other.ptr_; }

    // Compound assignment operators
    const_iterator &operator+=(difference_type offset) {
      ptr_ += offset;
      return *this;
    }

    const_iterator &operator-=(difference_type offset) {
      ptr_ -= offset;
      return *this;
    }

  private:
    pointer ptr_;
  };

  explicit FixedSizeArray(const size_t capacity)
      : k_capacity_(capacity), buffer_(new T[capacity]) {}

  ~FixedSizeArray() { delete[] buffer_; }

  // Copy constructor
  FixedSizeArray(const FixedSizeArray &other)
      : k_capacity_(other.k_capacity_), size_(other.size_), buffer_(new T[other.size_]) {
    std::copy(other.buffer_, other.buffer_ + other.size_, buffer_);
  }

  // Copy assignment operator
  auto operator=(const FixedSizeArray &other) -> FixedSizeArray & {
    if (this != &other) {
      delete[] buffer_;
      k_capacity_ = other.k_capacity_;
      size_ = other.size_;
      buffer_ = new T[other.size_];
      std::copy(other.buffer_, other.buffer_ + other.size_, buffer_);
    }
    return *this;
  }

  // Move constructor
  FixedSizeArray(FixedSizeArray &&other) noexcept
      : k_capacity_(other.k_capacity_), size_(other.size_), buffer_(other.buffer_) {
    other.buffer_ = nullptr;
    other.size_ = 0;
  }

  // Move assignment operator
  auto operator=(FixedSizeArray &&other) noexcept -> FixedSizeArray & {
    if (this != &other) {
      delete[] buffer_;
      k_capacity_ = other.k_capacity_;
      size_ = other.size_;
      buffer_ = other.buffer_;
      other.buffer_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  // Element access without range checking
  auto operator[](const size_t index) -> T & {
#ifndef NDEBUG
    if (index >= size_)
      throw std::out_of_range(std::format("Index {} out of range (size: {})", index, size_));
#endif

    return buffer_[index];
  }

  auto operator[](const size_t index) const -> const T & {
#ifndef NDEBUG
    if (index >= size_)
      throw std::out_of_range(std::format("Index {} out of range (size: {})", index, size_));
#endif

    return buffer_[index];
  }

  // Element access with range checking
  auto at(const size_t index) -> T & {
    if (index >= size_)
      throw std::out_of_range(std::format("Index {} out of range (size: {})", index, size_));
    return buffer_[index];
  }

  auto at(const size_t index) const -> const T & {
    if (index >= size_)
      throw std::out_of_range(std::format("Index {} out of range (size: {})", index, size_));
    return buffer_[index];
  }

  // Append an element to the end of the array
  void append(const T &element) {
#ifndef NDEBUG
    if (size_ == k_capacity_)
      throw std::overflow_error("Array is full");
#endif

    buffer_[size_++] = element;
  }

  // Begin iterator
  auto begin() -> iterator { return iterator(buffer_); }

  auto begin() const -> const_iterator { return const_iterator(buffer_); }
  auto cbegin() const -> const_iterator { return const_iterator(buffer_); }

  auto rbegin() -> std::reverse_iterator<iterator> {
    return std::reverse_iterator<iterator>(end());
  }

  auto rbegin() const -> std::reverse_iterator<const_iterator> {
    return std::reverse_iterator<const_iterator>(end());
  }
  auto crbegin() const -> std::reverse_iterator<const_iterator> {
    return std::reverse_iterator<const_iterator>(end());
  }

  // End iterator
  auto end() -> iterator { return iterator(buffer_ + size_); }

  auto end() const -> const_iterator { return const_iterator(buffer_ + size_); }
  auto cend() const -> const_iterator { return const_iterator(buffer_ + size_); }

  auto rend() -> std::reverse_iterator<iterator> {
    return std::reverse_iterator<iterator>(begin());
  }

  auto rend() const -> std::reverse_iterator<const_iterator> {
    return std::reverse_iterator<const_iterator>(begin());
  }
  auto crend() const -> std::reverse_iterator<const_iterator> {
    return std::reverse_iterator<const_iterator>(begin());
  }

  [[nodiscard]] auto capacity() const -> size_t { return k_capacity_; }

  [[nodiscard]] auto size() const -> size_t { return size_; }

  [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

  [[nodiscard]] auto full() const -> bool { return size_ == k_capacity_; }

private:
  size_t k_capacity_; // Maximum capacity of the array

  size_t size_ = 0; // Size of the array
  T *buffer_;       // Dynamically allocated buffer for elements
};
