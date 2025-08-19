#pragma once

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
#include <limits>
#include <string>
#include <string_view>
#include <version>

#include <mio/mmap.hpp>
#include <spdlog/spdlog.h>
#include <unordered_set>

struct Request {
  uint32_t timestamp;         // in seconds
  uint64_t obj_id;            // hash of object id (string)
  uint32_t obj_size;          // in bytes
  uint64_t next_access_vtime; // logical time

  static constexpr size_t UNALIGNED_SIZE =
      sizeof(timestamp) + sizeof(obj_id) + sizeof(obj_size) + sizeof(next_access_vtime);
};

/**
 * @brief A wrapper of an `.oracleGeneral` trace file supporting read-only iteration using mmap.
 */
class CachingTrace {
public:
  // Read-only iterator for CachingTrace
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = Request;
    using difference_type = std::ptrdiff_t;
    using pointer = const Request *;
    using reference = const Request &;

    iterator() : offset_(0), total_(0), end_(true), current_record_() {}

    iterator(std::string filename, size_t index, size_t total)
        : filename_(std::move(filename)), offset_(index), total_(total), end_(index >= total),
          current_record_() {
      if (!end_) {
        // Memory-map the file
        try {
          mmap_ = mio::mmap_source(filename_);
        } catch (const std::system_error &e) {
          throw std::ios_base::failure(
              std::format("Failed to open file in iterator: {}", filename_));
        }
        // Read the current record
        read_current();
      }
    }

    iterator(iterator &&other) noexcept
        : filename_(std::move(other.filename_)), offset_(other.offset_), total_(other.total_),
          end_(other.end_), mmap_(std::move(other.mmap_)), current_record_(other.current_record_) {
      other.end_ = true;
    }

    auto operator=(iterator &&other) noexcept -> iterator & {
      if (this != &other) {
        filename_ = std::move(other.filename_);
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
        : filename_(other.filename_), offset_(other.offset_), total_(other.total_),
          end_(other.end_), current_record_(other.current_record_) {
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

        if (!end_) {
          read_current();
        }
      }
      return *this;
    }

    ~iterator() = default;

    auto operator*() const -> const Request & { return current_record_; }

    auto operator->() const -> const Request * { return &current_record_; }

    auto operator++() -> iterator & {
      if (end_)
        return *this;
      ++offset_;
      if (offset_ >= total_) {
        end_ = true;
      } else {
        read_current();
      }
      return *this;
    }

    auto operator++(int) -> iterator {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    auto operator==(const iterator &other) const -> bool {
      return (end_ && other.end_) || (offset_ == other.offset_ && filename_ == other.filename_);
    }

    auto operator!=(const iterator &other) const -> bool { return !(*this == other); }

  private:
    void read_current() {
      if (end_)
        return;

      // Compute the offset based on the index and record size
      const auto offset = static_cast<std::streamoff>(offset_ * Request::UNALIGNED_SIZE);

      if (offset + Request::UNALIGNED_SIZE > mmap_.size()) {
        throw std::ios_base::failure(std::format("Index {} out of bounds.", offset_));
      }

      // Read the raw data into the record
      const char *data = &mmap_[offset];
      std::memcpy(&current_record_.timestamp, data, sizeof(current_record_.timestamp));
      std::memcpy(&current_record_.obj_id, data + sizeof(current_record_.timestamp),
                  sizeof(current_record_.obj_id));
      std::memcpy(&current_record_.obj_size,
                  data + sizeof(current_record_.timestamp) + sizeof(current_record_.obj_id),
                  sizeof(current_record_.obj_size));
      std::memcpy(&current_record_.next_access_vtime,
                  data + sizeof(current_record_.timestamp) + sizeof(current_record_.obj_id) +
                      sizeof(current_record_.obj_size),
                  sizeof(current_record_.next_access_vtime));

      // If next_access_vtime is -1, set it to max value
      if (static_cast<int64_t>(current_record_.next_access_vtime) == -1)
        current_record_.next_access_vtime = std::numeric_limits<uint64_t>::max();
    }

    std::string filename_;   // File path
    size_t offset_;          // Current index
    size_t total_;           // Total number of records
    bool end_;               // End flag
    mio::mmap_source mmap_;  // Memory-mapped file
    Request current_record_; // Current record
  };

  // Constructor, open file and read total number of records
  explicit CachingTrace(const std::string_view pathname) : filepath_(pathname) {
    try {
      mmap_ = mio::mmap_source(filepath_);
    } catch (const std::system_error &e) {
      throw std::ios_base::failure(std::format("Failed to open file: {}", pathname));
    }

    // Check file size and compute the number of entries
    if (mmap_.size() % Request::UNALIGNED_SIZE != 0)
      throw std::ios_base::failure(std::format(
          "File size is not a multiple of record size ({} bytes).", Request::UNALIGNED_SIZE));
    num_entries_ = mmap_.size() / Request::UNALIGNED_SIZE;
  }

  ~CachingTrace() = default;

  CachingTrace(const CachingTrace &other)
      : filepath_(other.filepath_), num_entries_(other.num_entries_) {
    try {
      mmap_ = mio::mmap_source(filepath_); // Re-map the file content
    } catch (const std::system_error &e) {
      throw std::ios_base::failure(
          std::format("Failed to copy mmap in CachingTrace: {}", filepath_));
    }
  }

  CachingTrace &operator=(const CachingTrace &other) {
    if (this != &other) {
      filepath_ = other.filepath_;
      num_entries_ = other.num_entries_;
      try {
        mmap_ = mio::mmap_source(filepath_); // Re-map the file content
      } catch (const std::system_error &e) {
        throw std::ios_base::failure(
            std::format("Failed to copy mmap in CachingTrace: {}", filepath_));
      }
    }
    return *this;
  }

  CachingTrace(CachingTrace &&other) noexcept
      : filepath_(std::move(other.filepath_)), num_entries_(other.num_entries_),
        mmap_(std::move(other.mmap_)) {
    other.num_entries_ = 0;
  }

  CachingTrace &operator=(CachingTrace &&other) noexcept {
    if (this != &other) {
      filepath_ = std::move(other.filepath_);
      num_entries_ = other.num_entries_;
      mmap_ = std::move(other.mmap_);
      other.num_entries_ = 0;
    }
    return *this;
  }

  auto operator[](size_t index) const -> Request {
    if (index >= num_entries_)
      throw std::out_of_range(
          std::format("Index {} is out of range (total entries: {}).", index, num_entries_));

    // Read the record from the memory-mapped file
    const auto offset = static_cast<std::streamoff>(index * Request::UNALIGNED_SIZE);
    if (offset + Request::UNALIGNED_SIZE > mmap_.size())
      throw std::ios_base::failure(std::format("Index {} out of bounds in operator[].", index));

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    Request record;
    const char *data = &mmap_[offset];
    std::memcpy(&record.timestamp, data, sizeof(record.timestamp));
    std::memcpy(&record.obj_id, data + sizeof(record.timestamp), sizeof(record.obj_id));
    std::memcpy(&record.obj_size, data + sizeof(record.timestamp) + sizeof(record.obj_id),
                sizeof(record.obj_size));
    std::memcpy(&record.next_access_vtime,
                data + sizeof(record.timestamp) + sizeof(record.obj_id) + sizeof(record.obj_size),
                sizeof(record.next_access_vtime));

    // If next_access_vtime is -1, set it to max value
    if (static_cast<int64_t>(record.next_access_vtime) == -1)
      record.next_access_vtime = std::numeric_limits<uint64_t>::max();

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
};

inline auto get_cache_dir() -> std::filesystem::path {
  const auto cwd = std::filesystem::current_path();
  const auto cache_dir = cwd / ".cache" / "benchmark";
  if (!std::filesystem::exists(cache_dir))
    std::filesystem::create_directories(cache_dir);
  return cache_dir;
}

/**
 * @brief Count unique object IDs in a cache trace, with persistent file-based cache.
 *
 * @param trace Reference to a trace object.
 * @param use_cache Whether to read/write cache files (default: true).
 * @return Number of unique object IDs (size_t).
 */
inline auto count_unique_objects(const CachingTrace &trace, bool use_cache = true) -> size_t {
  // Determine cache directory
  const auto cache_dir = get_cache_dir();

  const std::filesystem::path file_path = trace.filepath();

  // Get last write time and convert to milliseconds since epoch
  const auto ftime = std::filesystem::last_write_time(file_path);
#if __cpp_lib_chrono >= 201907L
  const auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
#else
  const auto sys_time = std::chrono::file_clock::to_sys(ftime);
#endif
  const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(sys_time);
  const auto mtime_ms = ms.time_since_epoch().count();

  const auto basename = file_path.filename().string();
  const std::string cache_key_prefix = "unique_objects_" + basename + "_";
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
  std::unordered_set<uint64_t> id_set;
  for (const auto &req : trace)
    id_set.insert(req.obj_id);
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
