#pragma once

#include <cstddef>

#ifndef NDEBUG
#include <unordered_set>
#endif

#include <spdlog/spdlog.h>

template <typename T> struct Node {
  T value;
  Node<T> *prev;
  Node<T> *next;
};

template <typename T> class DoublyLinkedList {

public:
  DoublyLinkedList() : head_(nullptr), tail_(nullptr) {}

  ~DoublyLinkedList() {
    Node<T> *current = head_;
    while (current != nullptr) {
      Node<T> *temp = current;
      current = current->next;
      delete temp;
    }
  }

  DoublyLinkedList(const DoublyLinkedList &other) : head_(nullptr), tail_(nullptr) {
    Node<T> *current = other.tail_;
    while (current != nullptr) {
#ifndef NDEBUG
      debug_node_set_.insert(insert(current->value));
#else
      insert(current->value);
#endif
      current = current->prev;
    }
  }

  DoublyLinkedList(DoublyLinkedList &&other) noexcept
      : head_(other.head_), tail_(other.tail_), size_(other.size_)
#ifndef NDEBUG
        ,
        debug_node_set_(std::move(other.debug_node_set_))
#endif
  {
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.size_ = 0;
  }

  auto operator=(const DoublyLinkedList &other) -> DoublyLinkedList & {
    if (this == &other)
      return *this;

    // Clean up current list
    Node<T> *current = tail_;
    while (current != nullptr) {
      Node<T> *temp = current;
      current = current->prev;
      delete temp;
    }

    // Copy nodes from other list
    head_ = tail_ = nullptr;
    current = other.tail_;
    while (current != nullptr) {
#ifndef NDEBUG
      debug_node_set_.insert(insert(current->value));
#else
      insert(current->value);
#endif
      current = current->prev;
    }

    return *this;
  }

  auto operator=(DoublyLinkedList &&other) noexcept -> DoublyLinkedList & {
    if (this == &other)
      return *this;

    // Clean up current list
    Node<T> *current = tail_;
    while (current != nullptr) {
      Node<T> *temp = current;
      current = current->prev;
      delete temp;
    }

    // Transfer ownership from other
    head_ = other.head_;
    tail_ = other.tail_;
    size_ = other.size_;
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.size_ = 0;
#ifndef NDEBUG
    debug_node_set_ = std::move(other.debug_node_set_);
#endif

    return *this;
  }

  Node<T> *head() const { return head_; }
  Node<T> *tail() const { return tail_; }

  [[nodiscard]] auto empty() const -> bool { return head_ == nullptr; }

  /**
   * @brief Insert a value at the head of the list and return the node.
   *
   * Time complexity: O(1)
   *
   * @param value The value to insert.
   * @return The node containing the value.
   */
  auto insert(T value) -> Node<T> * {
    auto *node = new Node<T>;
    node->value = value;
    node->prev = nullptr;
    node->next = head_;

    if (head_ == nullptr) {
      head_ = tail_ = node;
    } else {
      head_->prev = node;
      head_ = node;
    }

#ifndef NDEBUG
    debug_node_set_.insert(node);
#endif

    size_++;

    return node;
  }

  void insert_tail(T value) {
    auto *node = new Node<T>;
    node->value = value;
    node->prev = tail_;
    node->next = nullptr;

    if (tail_ == nullptr) {
      tail_ = head_ = node;
    } else {
      tail_->next = node;
      tail_ = node;
    }

#ifndef NDEBUG
    debug_node_set_.insert(node);
#endif

    size_++;
  }

  auto insert_before(Node<T> *node, T value) -> Node<T> * {
#ifndef NDEBUG
    if (node == nullptr) {
      spdlog::warn("DoublyLinkedList: Suspicious `insert_before` call with nullptr node");
    } else {
      // Make sure the node is in the list
      if (!debug_contains(node))
        spdlog::warn("DoublyLinkedList: Suspicious `insert_before` call with node not in the list");
    }
#endif

    auto *new_node = new Node<T>;
    new_node->value = value;
    new_node->prev = node->prev;
    new_node->next = node;

    if (node->prev != nullptr)
      node->prev->next = new_node;
    else
      head_ = new_node;

    node->prev = new_node;

#ifndef NDEBUG
    debug_node_set_.insert(new_node);
#endif

    size_++;

    return new_node;
  }

  auto insert_after(Node<T> *node, T value) -> Node<T> * {
#ifndef NDEBUG
    if (node == nullptr) {
      spdlog::warn("DoublyLinkedList: Suspicious `insert_after` call with nullptr node");
    } else {
      // Make sure the node is in the list
      if (!debug_contains(node))
        spdlog::warn("DoublyLinkedList: Suspicious `insert_after` call with node not in the list");
    }
#endif

    auto *new_node = new Node<T>;
    new_node->value = value;
    new_node->prev = node;
    new_node->next = node->next;

    if (node->next != nullptr)
      node->next->prev = new_node;
    else
      tail_ = new_node;

    node->next = new_node;

#ifndef NDEBUG
    debug_node_set_.insert(new_node);
#endif

    size_++;

    return new_node;
  }

  /**
   * @brief Remove a node from the list and free the memory.
   *
   * Time complexity: O(1)
   *
   * @param node The node to remove.
   */
  void remove_node(Node<T> *node) {
#ifndef NDEBUG
    if (node == nullptr) {
      spdlog::warn("DoublyLinkedList: Suspicious `remove_node` call with nullptr node");
    } else {
      // Make sure the node is in the list
      if (!debug_contains(node))
        spdlog::warn("DoublyLinkedList: Suspicious `remove_node` call with node not in the list");
    }
#endif

    if (node->prev != nullptr)
      node->prev->next = node->next;
    else
      head_ = node->next;

    if (node->next != nullptr)
      node->next->prev = node->prev;
    else
      tail_ = node->prev;

#ifndef NDEBUG
    debug_node_set_.erase(node);
#endif

    size_--;

    delete node;
  }

  /**
   * @brief Remove the head node and free the memory.
   *
   * Time complexity: O(1)
   */
  void remove_head() {
#ifndef NDEBUG
    if (head_ == nullptr)
      spdlog::warn("DoublyLinkedList: Suspicious `remove_head` call with empty list");
#endif

    Node<T> *node = head_;
    head_ = head_->next;
    if (head_ != nullptr)
      head_->prev = nullptr;
    else
      tail_ = nullptr;

#ifndef NDEBUG
    debug_node_set_.erase(node);
#endif

    size_--;

    delete node;
  }

  /**
   * @brief Remove the tail node and free the memory.
   *
   * Time complexity: O(1)
   */
  void remove_tail() {
#ifndef NDEBUG
    if (tail_ == nullptr)
      spdlog::warn("DoublyLinkedList: Suspicious `remove_tail` call with empty list");
#endif

    Node<T> *node = tail_;
    tail_ = tail_->prev;
    if (tail_ != nullptr)
      tail_->next = nullptr;
    else
      head_ = nullptr;

#ifndef NDEBUG
    debug_node_set_.erase(node);
#endif

    size_--;

    delete node;
  }

  /**
   * @brief Transfer a node to the tail of another list.
   *
   * Time complexity: O(1)
   *
   * Warning: You must ensure that the node is in the list before calling this function.
   *
   * @param node The node to transfer.
   * @param list The list to transfer the node to.
   */
  void transfer_node_to_head_of(Node<T> *node, DoublyLinkedList<T> &list) {
#ifndef NDEBUG
    if (node == nullptr) {
      spdlog::warn(
          "DoublyLinkedList: Suspicious `transfer_node_to_head_of` call with nullptr node");
    } else {
      // Make sure the node is in the list
      if (!debug_contains(node))
        spdlog::warn("DoublyLinkedList: Suspicious `transfer_node_to_head_of` call with node not "
                     "in the list");
    }
#endif

    if (node->next != nullptr)
      node->next->prev = node->prev;
    else
      tail_ = node->prev;

    if (node->prev != nullptr)
      node->prev->next = node->next;
    else
      head_ = node->next;

    if (list.head_ == nullptr) {
      list.head_ = list.tail_ = node;
      node->next = nullptr;
    } else {
      list.head_->prev = node;
      node->next = list.head_;
      list.head_ = node;
    }

    node->prev = nullptr;

#ifndef NDEBUG
    debug_node_set_.erase(node);
    list.debug_node_set_.insert(node);
#endif

    size_--;
    list.size_++;
  }

  /**
   * @brief Transfer the tail node to the head of another list.
   *
   * Time complexity: O(1)
   *
   * @param list The list to transfer the node to.
   * @return The transferred node (i.e., the original head node).
   */
  auto transfer_tail_to_head_of(DoublyLinkedList<T> &list) -> Node<T> * {
#ifndef NDEBUG
    if (head_ == nullptr)
      spdlog::warn("DoublyLinkedList: Suspicious `transfer_tail_to_head_of` call with empty list");
#endif

    Node<T> *node = tail_;
    tail_ = tail_->prev;
    if (tail_ != nullptr)
      tail_->next = nullptr;
    else
      head_ = nullptr;

    if (list.head_ == nullptr) {
      list.head_ = list.tail_ = node;
      node->next = nullptr;
    } else {
      list.head_->prev = node;
      node->next = list.head_;
      list.head_ = node;
    }

    node->prev = nullptr;

#ifndef NDEBUG
    debug_node_set_.erase(node);
    list.debug_node_set_.insert(node);
#endif

    size_--;
    list.size_++;

    return node;
  }

  /**
   * @brief Move a node in the list to the head.
   *
   * Time complexity: O(1)
   *
   * Warning: You must ensure that the node is in the list before calling this function.
   *
   * @param node The node to move.
   */
  void move_to_head(Node<T> *node) {
#ifndef NDEBUG
    if (node == nullptr) {
      spdlog::warn("DoublyLinkedList: Suspicious `move_to_head` call with nullptr node");
    } else {
      // Make sure the node is in the list
      if (!debug_contains(node))
        spdlog::warn("DoublyLinkedList: Suspicious `move_to_head` call with node not in the list");
    }
#endif

    if (node == nullptr || node == head_)
      return;

    // Remove node from its current position
    if (node->next != nullptr)
      node->next->prev = node->prev;
    else
      tail_ = node->prev;

    if (node->prev != nullptr)
      node->prev->next = node->next;
    else
      head_ = node->next;

    // Insert node at the tail
    node->next = head_;
    node->prev = nullptr;
    if (head_ != nullptr)
      head_->prev = node;
    head_ = node;

    if (tail_ == nullptr)
      tail_ = node;
  }

  void move_tail_to_head() {
#ifndef NDEBUG
    if (head_ == nullptr)
      spdlog::warn("DoublyLinkedList: Suspicious `move_tail_to_head` call with empty list");
#endif

    if (tail_ == head_)
      return;

    Node<T> *node = tail_;
    tail_ = tail_->prev;
    tail_->next = nullptr;

    head_->prev = node;
    node->next = head_;
    node->prev = nullptr;
    head_ = node;
  }

  /**
   * @brief Get the size of the list.
   *
   * Time complexity: O(1)
   *
   * @return Number of elements in the list.
   */
  [[nodiscard]] auto size() const -> size_t { return size_; }

private:
  Node<T> *head_;
  Node<T> *tail_;

  size_t size_ = 0;

#ifndef NDEBUG
  std::unordered_set<Node<T> *> debug_node_set_;

  [[nodiscard]] auto debug_contains(Node<T> *node) const -> bool {
    return debug_node_set_.contains(node);
  }
#endif
};
