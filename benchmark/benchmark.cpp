#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fplus/fplus.hpp>
#include <spdlog/spdlog.h>
#include <vector>

#include "caching/reader.hpp"
#include "hm/reader.hpp"
#include "utils/benchmark.hpp"

BENCHMARK("caching", {.parallel = false}) {
  if (args.size() < 3)
    throw usage_error("<trace_path> <cache_size_ratio> <α1,α2,...>");

  const std::string &trace_path = args[0];
  const double cache_size_ratio = std::stod(args[1]);
  const std::vector<std::string> alphas = fplus::split(',', false, args[2]);

  std::string result_csv_miss_ratio = "alpha," + fplus::join_elem(',', available_benchmark_names());
  std::string result_csv_update_avg_time =
      "alpha," + fplus::join_elem(',', available_benchmark_names());
  std::string result_csv_estimate_avg_time =
      "alpha," + fplus::join_elem(',', available_benchmark_names());

  // Read trace
  spdlog::info("Reading trace from \"{}\"...", trace_path);
  const CachingTrace trace(trace_path);

  // Calculate unique object count
  const size_t object_count = count_unique_objects(trace);
  spdlog::info("#requests={}, #objects={}", trace.size(), object_count);
  const auto cache_size = static_cast<size_t>(static_cast<double>(object_count) * cache_size_ratio);
  spdlog::info("Cache size: {} ({}% of #objects)", cache_size, cache_size_ratio * 100);

  // Print first 5 requests
  spdlog::info("First 5 requests:");
  for (size_t i = 0; i < 5 && i < trace.size(); i++) {
    const auto &req = trace[i];
    spdlog::info("  {}: timestamp={}, obj_id={}, obj_size={}, next_access_vtime={}{}", i,
                 req.timestamp, req.obj_id, req.obj_size, req.next_access_vtime,
                 i == (trace.size() < 5 ? trace.size() - 1 : 4) ? "\n" : "");
  }

  // Benchmark
  std::unordered_map<std::string, std::unordered_map<std::string, double>> miss_ratios;
  std::unordered_map<std::string, std::unordered_map<std::string, double>> update_avg_times;
  std::unordered_map<std::string, std::unordered_map<std::string, double>> estimate_avg_times;
  std::mutex map_mutex;

  on_benchmark_finished([&](const auto name, const auto &args, const std::vector<double> &results,
                            const double time_spent) {
    std::lock_guard<std::mutex> lock(map_mutex);

    const std::string &alpha = args[2];
    const double miss_ratio = results[0];
    const double update_time_avg_seconds = results[1];
    const double estimate_time_avg_seconds = results[2];

    // if (update_time_avg_seconds == 0.0) {
    //   miss_ratios[alpha][std::string(name)] = miss_ratio;
    //   spdlog::info("[α={}] {}: (Miss Ratio) {:.6f}% ({:.6f}s elapsed)", alpha, name, time_spent);
    //   return;
    // }

    miss_ratios[alpha][std::string(name)] = miss_ratio;
    if (update_time_avg_seconds != 0.0) {
      update_avg_times[alpha][std::string(name)] = update_time_avg_seconds;
      estimate_avg_times[alpha][std::string(name)] = estimate_time_avg_seconds;
    }
    spdlog::info(
        "[α={}] {}: (Miss Ratio) {:.6f}%{} ({:.6f}s elapsed)", alpha, name, miss_ratio * 100,
        update_time_avg_seconds != 0.0 ? std::format(", (Update) {:.6f}MOps, (Estimate) {:.6f}MOps",
                                                     1.0 / update_time_avg_seconds / 1'000'000,
                                                     1.0 / estimate_time_avg_seconds / 1'000'000)
                                       : "",
        time_spent);
  });

  auto append_csv = [&](const auto &alpha) {
    result_csv_miss_ratio += "\n" + std::format("{}", alpha);
    for (const auto &name : available_benchmark_names()) {
      if (const auto it = miss_ratios.find(alpha); it != miss_ratios.end())
        if (const auto it2 = it->second.find(name); it2 != it->second.end())
          result_csv_miss_ratio += "," + std::format("{}", it2->second);
        else
          result_csv_miss_ratio += ",N/A"; // If the benchmark was not run
      else
        result_csv_miss_ratio += ",N/A"; // If no results for this alpha
    }

    result_csv_update_avg_time += "\n" + std::format("{}", alpha);
    for (const auto &name : available_benchmark_names()) {
      if (const auto it = update_avg_times.find(alpha); it != update_avg_times.end())
        if (const auto it2 = it->second.find(name); it2 != it->second.end())
          result_csv_update_avg_time += "," + std::format("{}", it2->second);
        else
          result_csv_update_avg_time += ",N/A"; // If the benchmark was not run
      else
        result_csv_update_avg_time += ",N/A"; // If no results for this alpha
    }

    result_csv_estimate_avg_time += "\n" + std::format("{}", alpha);
    for (const auto &name : available_benchmark_names()) {
      if (const auto it = estimate_avg_times.find(alpha); it != estimate_avg_times.end())
        if (const auto it2 = it->second.find(name); it2 != it->second.end())
          result_csv_estimate_avg_time += "," + std::format("{}", it2->second);
        else
          result_csv_estimate_avg_time += ",N/A"; // If the benchmark was not run
      else
        result_csv_estimate_avg_time += ",N/A"; // If no results for this alpha
    }
  };

  if (options.parallel) {
    for (const auto &alpha : alphas) {
      spdlog::info("Running benchmark with α={}...", alpha);

      miss_ratios.clear(); // Clear previous results
      benchmark_all(trace_path, cache_size, alpha);
    }

    wait();
    std::cout << std::endl;

    for (const auto &alpha : alphas) {
      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(
          miss_ratios[alpha].begin(), miss_ratios[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::cout << std::endl;

      // Append results to CSV
      append_csv(alpha);
    }
  } else {
    for (const auto &alpha : alphas) {
      spdlog::info("Running benchmark with α={}...", alpha);

      miss_ratios.clear(); // Clear previous results
      benchmark_all(trace_path, cache_size, alpha);
      wait();
      std::cout << std::endl;

      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(
          miss_ratios[alpha].begin(), miss_ratios[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::cout << std::endl;

      // Append results to CSV
      append_csv(alpha);
    }
  }

  // Print results as CSV
  spdlog::info("Miss Ratios (CSV format):\n{}", result_csv_miss_ratio);
  spdlog::info("Average Update Time by Seconds (CSV format):\n{}", result_csv_update_avg_time);
  spdlog::info("Average Estimate Time by Seconds (CSV format):\n{}", result_csv_estimate_avg_time);
}

BENCHMARK("hm", {.parallel = false}) {
  if (args.size() < 4)
    throw usage_error("<trace_path> <cache_size_ratio> <top_k> <α1,α2,...>");

  const std::string &trace_path = args[0];
  const double cache_size_ratio = std::stod(args[1]);
  const size_t top_k = std::stoul(args[2]);
  const std::vector<std::string> alphas = fplus::split(',', false, args[3]);

  std::string result_csv_coverage = "alpha," + fplus::join_elem(',', available_benchmark_names());
  std::string result_csv_update_avg_time =
      "alpha," + fplus::join_elem(',', available_benchmark_names());
  std::string result_csv_estimate_avg_time =
      "alpha," + fplus::join_elem(',', available_benchmark_names());

  // Read trace
  spdlog::info("Reading trace from \"{}\"...", trace_path);
  const TransactionTrace trace(trace_path);

  // Calculate unique IP addresses
  const size_t unique_products = count_unique_products(trace);
  spdlog::info("#transactions={}, #unique_products={}", trace.size(), unique_products);
  const auto cache_size =
      static_cast<size_t>(static_cast<double>(unique_products) * cache_size_ratio);
  spdlog::info("Cache size: {} ({}% of #unique_products)", cache_size, cache_size_ratio * 100);

  // Print first 5 packets
  spdlog::info("First 5 transactions:");
  for (size_t i = 0; i < 5 && i < trace.size(); i++) {
    const auto &trans = trace[i];
    spdlog::info("  {}: {}{}", i, trans.product_code,
                 i == (trace.size() < 5 ? trace.size() - 1 : 4) ? "\n" : "");
  }

  // Benchmark
  std::unordered_map<std::string, std::unordered_map<std::string, double>> coverages;
  std::unordered_map<std::string, std::unordered_map<std::string, double>> update_avg_times;
  std::unordered_map<std::string, std::unordered_map<std::string, double>> estimate_avg_times;
  std::mutex map_mutex;

  on_benchmark_finished([&](const auto name, const auto &args, const std::vector<double> &results,
                            const double time_spent) {
    std::lock_guard<std::mutex> lock(map_mutex);

    const std::string &alpha = args[3];
    const double miss_ratio = results[0];
    const double update_time_avg_seconds = results[1];
    const double estimate_time_avg_seconds = results[2];

    coverages[alpha][std::string(name)] = miss_ratio;
    update_avg_times[alpha][std::string(name)] = update_time_avg_seconds;
    estimate_avg_times[alpha][std::string(name)] = estimate_time_avg_seconds;
    spdlog::info(
        "[α={}] {}: (Coverage) {:.6f}%, (Update) {:.6f}MOps, (Estimate) {:.6f}MOps ({:.6f}s "
        "elapsed)",
        fplus::trim_right('.', fplus::trim_right('0', std::format("{:f}", std::stod(alpha)))), name,
        miss_ratio * 100, 1.0 / update_time_avg_seconds / 1'000'000,
        1.0 / estimate_time_avg_seconds / 1'000'000, time_spent);
  });

  auto append_csv = [&](const auto &alpha) {
    result_csv_coverage += "\n" + std::format("{}", alpha);
    for (const auto &name : available_benchmark_names()) {
      if (const auto it = coverages.find(alpha); it != coverages.end())
        if (const auto it2 = it->second.find(name); it2 != it->second.end())
          result_csv_coverage += "," + std::format("{}", it2->second);
        else
          result_csv_coverage += ",N/A"; // If the benchmark was not run
      else
        result_csv_coverage += ",N/A"; // If no results for this alpha
    }

    result_csv_update_avg_time += "\n" + std::format("{}", alpha);
    for (const auto &name : available_benchmark_names()) {
      if (const auto it = update_avg_times.find(alpha); it != update_avg_times.end())
        if (const auto it2 = it->second.find(name); it2 != it->second.end())
          result_csv_update_avg_time += "," + std::format("{}", it2->second);
        else
          result_csv_update_avg_time += ",N/A"; // If the benchmark was not run
      else
        result_csv_update_avg_time += ",N/A"; // If no results for this alpha
    }

    result_csv_estimate_avg_time += "\n" + std::format("{}", alpha);
    for (const auto &name : available_benchmark_names()) {
      if (const auto it = estimate_avg_times.find(alpha); it != estimate_avg_times.end())
        if (const auto it2 = it->second.find(name); it2 != it->second.end())
          result_csv_estimate_avg_time += "," + std::format("{}", it2->second);
        else
          result_csv_estimate_avg_time += ",N/A"; // If the benchmark was not run
      else
        result_csv_estimate_avg_time += ",N/A"; // If no results for this alpha
    }
  };

  if (options.parallel) {
    for (const auto &alpha : alphas) {
      spdlog::info("Running H&M Trending (k={}) benchmark with α={}...", top_k, alpha);

      coverages.clear(); // Clear previous results
      benchmark_all(trace_path, cache_size, top_k, alpha);
    }

    wait();
    std::cout << std::endl;

    for (const auto &alpha : alphas) {
      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(coverages[alpha].begin(),
                                                                          coverages[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::cout << std::endl;

      // Append results to CSV
      append_csv(alpha);
    }
  } else {
    for (const auto &alpha : alphas) {
      spdlog::info("Running H&M Trending (k={}) benchmark with α={}...", top_k, alpha);

      coverages.clear(); // Clear previous results
      benchmark_all(trace_path, cache_size, top_k, alpha);
      wait();
      std::cout << std::endl;

      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(coverages[alpha].begin(),
                                                                          coverages[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::cout << std::endl;

      // Append results to CSV
      append_csv(alpha);
    }
  }

  // Print results as CSV
  spdlog::info("Coverages (CSV format):\n{}", result_csv_coverage);
  spdlog::info("Average Update Time by Seconds (CSV format):\n{}", result_csv_update_avg_time);
  spdlog::info("Average Estimate Time by Seconds (CSV format):\n{}", result_csv_estimate_avg_time);
}

/********
 * Main *
 ********/
BENCHMARK_MAIN() {
  // Change "info" to "debug" to see more detailed logs
  spdlog::set_level(spdlog::level::info);

  return 0;
}
