#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <print>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct Request {
  uint32_t timestamp;        // in seconds
  uint64_t obj_id;           // object id (use key)
  uint32_t obj_size;         // in bytes (use key_size+size)
  int64_t next_access_vtime; // logical time (index of next access)
};

auto main(int argc, char *argv[]) -> int {
  if (argc != 3) {
    std::println(std::cerr, "Usage: {} <input.csv> <output.oracleGeneral>", argv[0]);
    return 1;
  }

  const std::string in_path = argv[1];
  const std::string out_path = argv[2];

  std::ifstream is(in_path);
  if (!is) {
    std::println(std::cerr, "Error: cannot open {}", in_path);
    return 1;
  }

  std::vector<Request> requests;
  std::unordered_map<uint64_t, uint32_t> obj_sizes; // Handle cache misses
  std::unordered_map<uint64_t, std::vector<size_t>> access_vtimes;

  auto hash = std::hash<std::string>{};

  size_t time_idx = std::numeric_limits<size_t>::max();
  size_t key_idx = std::numeric_limits<size_t>::max();
  size_t op_idx = std::numeric_limits<size_t>::max();
  size_t op_count_idx = std::numeric_limits<size_t>::max();
  size_t key_size_idx = std::numeric_limits<size_t>::max();
  size_t size_idx = std::numeric_limits<size_t>::max();
  bool use_hash = false;

  std::string line;
  while (std::getline(is, line)) {
    std::stringstream ss(line);
    std::string token;
    std::vector<std::string> fields;
    while (std::getline(ss, token, ','))
      fields.push_back(token);
    if (fields.size() < 5) {
      std::println(std::cerr, "Warning: skipping line with insufficient fields: {}", line);
      continue;
    }

    if (fields[0] == "key" || fields[0] == "op_time") {
      // Skip header
      time_idx = std::ranges::find(fields, "op_time") - fields.begin();
      if (time_idx == fields.size())
        time_idx = std::numeric_limits<size_t>::max();
      key_idx = std::ranges::find(fields, "key") - fields.begin();
      if (key_idx == fields.size()) {
        std::println(std::cerr, "Error: 'key' field not found in header");
        return 1;
      }
      op_idx = std::ranges::find(fields, "op") - fields.begin();
      if (op_idx == fields.size()) {
        std::println(std::cerr, "Error: 'op' field not found in header");
        return 1;
      }
      op_count_idx = std::ranges::find(fields, "op_count") - fields.begin();
      if (op_count_idx == fields.size()) {
        std::println(std::cerr, "Error: 'op_count' field not found in header");
        return 1;
      }
      key_size_idx = std::ranges::find(fields, "key_size") - fields.begin();
      if (key_size_idx == fields.size()) {
        std::println(std::cerr, "Error: 'key_size' field not found in header");
        return 1;
      }
      size_idx = std::ranges::find(fields, "size") - fields.begin();
      if (size_idx == fields.size()) {
        std::println(std::cerr, "Error: 'size' field not found in header");
        return 1;
      }
      if (std::ranges::find(fields, "usecase") != fields.end())
        use_hash = true;
      continue;
    }

    // Parse and convert
    const uint32_t timestamp = time_idx != std::numeric_limits<size_t>::max()
                                   ? static_cast<uint32_t>(std::stoul(fields[time_idx]) / 1000)
                                   : 0;
    const uint64_t key = use_hash ? hash(fields[key_idx]) : std::stoull(fields[key_idx]);
    const std::string op = fields[op_idx];
    const size_t op_count = std::stoul(fields[op_count_idx]);
    const auto key_size = static_cast<uint32_t>(std::stoul(fields[key_size_idx]));
    auto size = static_cast<uint32_t>(std::stoul(fields[size_idx]));

    if (op == "DELETE")
      // Skip deletes
      continue;

    if (size == 0) {
      // Use the last known size for this object
      const auto it = obj_sizes.find(key);
      if (it != obj_sizes.end())
        size = it->second;
    } else {
      // Update the size for this object
      obj_sizes[key] = size;
    }

    for (size_t i = 0; i < op_count; i++) {
      access_vtimes[key].push_back(requests.size());
      requests.push_back({.timestamp = timestamp,
                          .obj_id = key,
                          .obj_size = key_size + size,
                          .next_access_vtime = -1});
    }
  }

  // Compute next_access_vtime for each request
  for (auto &[id, times] : access_vtimes) {
    for (size_t i = 0; i < times.size(); ++i) {
      requests[times[i]].next_access_vtime =
          (i + 1 < times.size()) ? static_cast<int64_t>(times[i + 1]) : -1;
    }
  }

  std::ofstream os(out_path, std::ios::binary);
  if (!os) {
    std::println(std::cerr, "Error: cannot create {}", out_path);
    return 1;
  }

  // Write requests to binary avoiding padding
  for (const auto &req : requests) {
    os.write(reinterpret_cast<const char *>(&req.timestamp), sizeof(req.timestamp));
    os.write(reinterpret_cast<const char *>(&req.obj_id), sizeof(req.obj_id));
    os.write(reinterpret_cast<const char *>(&req.obj_size), sizeof(req.obj_size));
    os.write(reinterpret_cast<const char *>(&req.next_access_vtime), sizeof(req.next_access_vtime));
  }
  os.close();

  std::println("Converted {} requests from {} to {}", requests.size(), in_path, out_path);
  return 0;
}
