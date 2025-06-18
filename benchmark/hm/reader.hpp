#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include <mio/mmap.hpp>
#include <spdlog/spdlog.h>
#include <unordered_set>

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct Transaction {
  uint32_t product_code;
};

inline auto get_hm_cache_dir() -> std::filesystem::path {
  const auto cwd = std::filesystem::current_path();
  const auto cache_dir = cwd / ".cache" / "benchmark";
  if (!std::filesystem::exists(cache_dir))
    std::filesystem::create_directories(cache_dir);
  return cache_dir;
}

/**
 * @brief Counts the number of lines in a file, with persistent file-based cache.
 *
 * @param path Path to the file.
 * @param use_cache Whether to read/write cache files (default: true).
 * @return Number of lines in the file (size_t).
 */
inline auto count_file_lines_hm(const std::filesystem::path &path, bool use_cache = true)
    -> size_t {
  // Determine cache directory
  const auto cache_dir = get_hm_cache_dir();

  // Get last write time and convert to milliseconds since epoch
  const auto ftime = std::filesystem::last_write_time(path);
#if defined(__APPLE__)
  const auto sys_time = std::chrono::file_clock::to_sys(ftime);
#else
  const auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
#endif
  const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(sys_time);
  const auto mtime_ms = ms.time_since_epoch().count();

  const auto basename = path.filename().string();
  const std::string cache_key_prefix = "file_lines_" + basename + "_";
  const std::string cache_key = cache_key_prefix + std::to_string(mtime_ms);

  // Full path to cache file
  const auto cache_file = cache_dir / cache_key;

  // Attempt to read from cache
  if (use_cache) {
    if (std::filesystem::exists(cache_file)) {
      std::ifstream ifs{cache_file, std::ios::in};
      if (ifs) {
        std::string content;
        std::getline(ifs, content);
        try {
          const size_t cached_count = std::stoull(content);
          return cached_count;
        } catch (const std::exception &e) {
          std::cerr << "Warning: failed to parse cached count (" << e.what() << ")\n";
          // Fall through to recompute
        }
      }
    }
  }

  // Compute line count
  std::ifstream file{path, std::ios::in};
  if (!file)
    throw std::ios_base::failure(std::format("Failed to open file: {}", path.string()));
  size_t count = std::ranges::count(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>(), '\n');

  // Write to cache and clean up old entries
  if (use_cache) {
    // Write current count
    {
      std::ofstream ofs{cache_file, std::ios::out | std::ios::trunc};
      ofs << count;
    }

    // Remove outdated cache files sharing the prefix (but not the current key)
    for (const auto &entry : std::filesystem::directory_iterator{cache_dir}) {
      if (!entry.is_regular_file())
        continue;
      const auto filename = entry.path().filename().string();
      if (filename.starts_with(cache_key_prefix) && filename != cache_key) {
        std::filesystem::remove(entry.path());
      }
    }
  }

  return count;
}

/**
 * @brief A wrapper of a transaction trace file supporting read-only iteration using mmap.
 */
class TransactionTrace {
public:
  // Read-only iterator for TransactionTrace
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = Transaction;
    using difference_type = std::ptrdiff_t;
    using pointer = const Transaction *;
    using reference = const Transaction &;

    iterator() : index_(0), offset_(0), total_(0), end_(true), current_record_() {}

    iterator(std::string filename, size_t index, size_t total)
        : filename_(std::move(filename)), index_(index), total_(total), end_(index >= total),
          current_record_() {
      if (!end_) {
        // Memory-map the file
        try {
          mmap_ = mio::mmap_source(filename_);
        } catch (const std::system_error &e) {
          throw std::ios_base::failure(
              std::format("Failed to open file in iterator: {}", filename_));
        }

        offset_ = index_to_offset(mmap_, index, total);

        // Read the current record
        read_current();
      }
    }

    iterator(iterator &&other) noexcept
        : filename_(std::move(other.filename_)), index_(other.index_), offset_(other.offset_),
          total_(other.total_), end_(other.end_), mmap_(std::move(other.mmap_)),
          current_record_(other.current_record_) {
      other.end_ = true;
    }

    auto operator=(iterator &&other) noexcept -> iterator & {
      if (this != &other) {
        filename_ = std::move(other.filename_);
        index_ = other.index_;
        offset_ = other.offset_;
        total_ = other.total_;
        end_ = other.end_;
        mmap_ = std::move(other.mmap_);
        current_record_ = other.current_record_;
        other.end_ = true;
      }
      return *this;
    }

    iterator(const iterator &other)
        : filename_(other.filename_), index_(other.index_), offset_(other.offset_),
          total_(other.total_), end_(other.end_), current_record_(other.current_record_) {
      try {
        mmap_ = mio::mmap_source(filename_); // Re-map the file content
      } catch (const std::system_error &e) {
        throw std::ios_base::failure(std::format("Failed to copy mmap in iterator: {}", filename_));
      }
      if (!end_) {
        read_current();
      }
    }

    auto operator=(const iterator &other) -> iterator & {
      if (this != &other) {
        filename_ = other.filename_;
        index_ = other.index_;
        offset_ = other.offset_;
        total_ = other.total_;
        end_ = other.end_;
        current_record_ = other.current_record_;

        try {
          mmap_ = mio::mmap_source(filename_); // Re-map the file content
        } catch (const std::system_error &e) {
          throw std::ios_base::failure(
              std::format("Failed to copy mmap in iterator: {}", filename_));
        }

        if (!end_)
          read_current();
      }
      return *this;
    }

    ~iterator() = default;

    auto operator*() const -> const Transaction & { return current_record_; }

    auto operator->() const -> const Transaction * { return &current_record_; }

    auto operator++() -> iterator & {
      if (end_)
        return *this;
      index_++;
      if (index_ >= total_)
        end_ = true;
      else
        read_current();
      return *this;
    }

    auto operator++(int) -> iterator {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    auto operator==(const iterator &other) const -> bool {
      return (end_ && other.end_) || (index_ == other.index_ && filename_ == other.filename_);
    }

    auto operator!=(const iterator &other) const -> bool { return !(*this == other); }

  private:
    void read_current() {
      if (end_)
        return;

      // Read the raw data into the record
      const char *data = &mmap_[offset_];

      size_t start = 0;
      size_t end = 0;
      while (offset_ + end < mmap_.size() && data[end] != ',')
        end++;

      start = end + 1; // Skip the comma
      end++;
      while (offset_ + end < mmap_.size() && data[end] != '\n')
        end++;
      current_record_.product_code =
          static_cast<uint32_t>(std::stoul(std::string(data + start, end - start)));

      offset_ += end; // Move to the next record
    }

    std::string filename_;       // File path
    size_t index_;               // Current index
    size_t offset_;              // Current offset
    size_t total_;               // Total number of records
    bool end_;                   // End flag
    mio::mmap_source mmap_;      // Memory-mapped file
    Transaction current_record_; // Current record
  };

  // Constructor, open file and read total number of records
  explicit TransactionTrace(const std::string_view pathname) : filepath_(pathname) {
    try {
      mmap_ = mio::mmap_source(filepath_);
    } catch (const std::system_error &e) {
      throw std::ios_base::failure(std::format("Failed to open file: {}", pathname));
    }

    // Compute the number of entries
    num_entries_ = count_file_lines_hm(filepath_) - 1; // Subtract 1 for the header line
  }

  ~TransactionTrace() = default;

  TransactionTrace(const TransactionTrace &other)
      : filepath_(other.filepath_), num_entries_(other.num_entries_) {
    try {
      mmap_ = mio::mmap_source(filepath_); // Re-map the file content
    } catch (const std::system_error &e) {
      throw std::ios_base::failure(
          std::format("Failed to copy mmap in TransactionTrace: {}", filepath_));
    }
  }

  auto operator=(const TransactionTrace &other) -> TransactionTrace & {
    if (this != &other) {
      filepath_ = other.filepath_;
      num_entries_ = other.num_entries_;
      try {
        mmap_ = mio::mmap_source(filepath_); // Re-map the file content
      } catch (const std::system_error &e) {
        throw std::ios_base::failure(
            std::format("Failed to copy mmap in TransactionTrace: {}", filepath_));
      }
    }
    return *this;
  }

  TransactionTrace(TransactionTrace &&other) noexcept
      : filepath_(std::move(other.filepath_)), num_entries_(other.num_entries_),
        mmap_(std::move(other.mmap_)) {
    other.num_entries_ = 0;
  }

  TransactionTrace &operator=(TransactionTrace &&other) noexcept {
    if (this != &other) {
      filepath_ = std::move(other.filepath_);
      num_entries_ = other.num_entries_;
      mmap_ = std::move(other.mmap_);
      other.num_entries_ = 0;
    }
    return *this;
  }

  auto operator[](size_t index) const -> Transaction {
    if (index >= num_entries_)
      throw std::out_of_range(
          std::format("Index {} is out of range (total entries: {}).", index, num_entries_));

    // Read the record from the memory-mapped file
    const size_t offset = index_to_offset(mmap_, index, num_entries_);
    const char *data = &mmap_[offset];

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    Transaction record;

    size_t start = 0;
    size_t end = 0;
    while (offset + end < mmap_.size() && data[end] != ',')
      end++;

    start = end + 1; // Skip the comma
    end++;
    while (offset + end < mmap_.size() && data[end] != '\n')
      end++;
    record.product_code = static_cast<uint32_t>(std::stoul(std::string(data + start, end - start)));

    return record;
  }

  [[nodiscard]] auto filepath() const noexcept -> const std::string & { return filepath_; }

  [[nodiscard]] auto num_entries() const noexcept -> size_t { return num_entries_; }

  [[nodiscard]] auto size() const noexcept -> size_t { return num_entries_; }

  [[nodiscard]] auto begin() const -> iterator { return {filepath_, 0, num_entries_}; }
  [[nodiscard]] auto rbegin() const -> iterator {
    return {filepath_, num_entries_ - 1, num_entries_};
  }

  [[nodiscard]] auto end() const -> iterator { return {filepath_, num_entries_, num_entries_}; }
  [[nodiscard]] auto rend() const -> iterator { return {filepath_, num_entries_, num_entries_}; }

private:
  std::string filepath_;   // File path
  size_t num_entries_ = 0; // Total number of records
  mio::mmap_source mmap_;  // Memory-mapped file

  static auto index_to_offset(const mio::mmap_source &mmap, const size_t index, const size_t total)
      -> std::size_t {
    if (index >= total)
      throw std::out_of_range(
          std::format("Index {} is out of range (total entries: {}).", index, total));
    size_t offset = 0;
    for (size_t i = 0; i < index + 1; i++) {
      while (offset < mmap.size() && mmap[offset] != '\n')
        offset++;
      offset++;
    }
    return offset;
  }
};

/**
 * @brief Counts unique products in a transcation trace, with persistent file-based cache.
 *
 * @param trace Reference to a trace object.
 * @param use_cache Whether to read/write cache files (default: true).
 * @return Number of unique IPs (size_t).
 */
inline auto count_unique_products(const TransactionTrace &trace, bool use_cache = true) -> size_t {
  // Determine cache directory
  const auto cache_dir = get_hm_cache_dir();

  const std::filesystem::path file_path = trace.filepath();

  // Get last write time and convert to milliseconds since epoch
  const auto ftime = std::filesystem::last_write_time(file_path);
#if defined(__APPLE__)
  const auto sys_time = std::chrono::file_clock::to_sys(ftime);
#else
  const auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
#endif
  const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(sys_time);
  const auto mtime_ms = ms.time_since_epoch().count();

  const auto basename = file_path.filename().string();
  const std::string cache_key_prefix = "unique_products_" + basename + "_";
  const std::string cache_key = cache_key_prefix + std::to_string(mtime_ms);

  // Full path to cache file
  const auto cache_file = cache_dir / cache_key;

  // Attempt to read from cache
  if (use_cache) {
    if (std::filesystem::exists(cache_file)) {
      std::ifstream ifs{cache_file, std::ios::in};
      if (ifs) {
        std::string content;
        std::getline(ifs, content);
        try {
          const size_t cached_count = std::stoull(content);
          return cached_count;
        } catch (const std::exception &e) {
          std::cerr << "Warning: failed to parse cached count (" << e.what() << ")\n";
          // Fall through to recompute
        }
      }
    }
  }

  // Compute unique-count
  std::unordered_set<uint32_t> id_set;
  for (const auto &transaction : trace)
    id_set.insert(transaction.product_code);
  const size_t unique_count = id_set.size();

  // Write to cache and clean up old entries
  if (use_cache) {
    // Write current count
    {
      std::ofstream ofs{cache_file, std::ios::out | std::ios::trunc};
      ofs << unique_count;
    }

    // Remove outdated cache files sharing the prefix (but not the current key)
    for (const auto &entry : std::filesystem::directory_iterator{cache_dir}) {
      if (!entry.is_regular_file())
        continue;
      const auto filename = entry.path().filename().string();
      if (filename.starts_with(cache_key_prefix) && filename != cache_key) {
        std::filesystem::remove(entry.path());
      }
    }
  }

  return unique_count;
}
