#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <fstream>
#include <mutex>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <argparse/argparse.hpp>
#include <fplus/fplus.hpp>
#include <spdlog/spdlog.h>
#include <tabulate/table.hpp>

#include "caching/reader.hpp"
#include "hm/reader.hpp"
#include "utils/benchmark.hpp"
#include "utils/errors.hpp"

BENCHMARK("caching") {
  argparse::ArgumentParser program;
  program.add_argument("trace_path").help("The path to the cache trace file");
  program.add_argument("cache_size_ratio")
      .help("The ratio of the cache size to the number of unique objects in the trace")
      .scan<'g', double>();
  program.add_argument("alphas").help(
      "Comma-separated list of alpha values to use (e.g., '0.1,0.2,0.3')");
  program.add_argument("-p", "--parallel")
      .help("Run all experiments in parallel")
      .default_value(DEFAULT_PARALLEL)
      .implicit_value(true);
  program.add_argument("-o", "--output").help("Output file path (as CSV)").default_value("");

  std::string trace_path;
  double cache_size_ratio;
  std::vector<std::string> alphas;
  std::string output_path;
  try {
    program.parse_args(argc, argv);
    trace_path = program.get<decltype(trace_path)>("trace_path");
    cache_size_ratio = program.get<decltype(cache_size_ratio)>("cache_size_ratio");
    options.parallel = program.get<bool>("--parallel");
    alphas = fplus::split(',', false, program.get<std::string>("alphas"));
    output_path = program.get<decltype(output_path)>("--output");
  } catch (const std::exception &e) {
    throw usage_error(program.help().str(), e.what());
  }

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

  if (options.parallel) {
    for (const auto &alpha : alphas) {
      spdlog::info("Running benchmark with α={}...", alpha);
      benchmark_all(trace_path, cache_size, alpha);
    }

    wait();
    std::println();

    for (const auto &alpha : alphas) {
      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(
          miss_ratios[alpha].begin(), miss_ratios[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::println();
    }
  } else {
    for (const auto &alpha : alphas) {
      spdlog::info("Running benchmark with α={}...", alpha);

      benchmark_all(trace_path, cache_size, alpha);
      wait();
      std::println();

      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(
          miss_ratios[alpha].begin(), miss_ratios[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::println();
    }
  }

  std::vector<std::tuple<std::string, std::string,
                         std::unordered_map<std::string, std::unordered_map<std::string, double>>>>
      result_maps = {
          {"miss_ratio", "Miss Ratios", miss_ratios},
          {"update_avg_time_s", "Average Update Time by Seconds", update_avg_times},
          {"estimate_avg_time_s", "Average Estimate Time by Seconds", estimate_avg_times},
      };

  std::unordered_map<std::string, std::vector<std::vector<std::variant<double, std::string>>>>
      results;
  for (const auto &[type, _, map] : result_maps) {
    std::vector<std::vector<std::variant<double, std::string>>> result;
    for (const auto &alpha : alphas) {
      std::vector<std::variant<double, std::string>> row;
      row.emplace_back(alpha);
      for (const auto &name : enabled_benchmark_names()) {
        if (const auto it = map.find(alpha); it != map.end())
          if (const auto it2 = it->second.find(name); it2 != it->second.end())
            row.emplace_back(it2->second);
          else
            row.emplace_back("N/A"); // If the benchmark was not run
        else
          row.emplace_back("N/A"); // If no results for this alpha
      }
      result.emplace_back(std::move(row));
    }
    results[type] = result;
  }

  // Print results
  for (const auto &[type, desc, _] : result_maps) {
    std::println("{}{}:", type == std::get<0>(result_maps[0]) ? "" : "\n", desc);
    tabulate::Table table;
    tabulate::Table::Row_t header{"Alpha"};
    for (const auto &name : enabled_benchmark_names())
      header.emplace_back(name);
    table.add_row(header);
    for (const auto &rows : results[type]) {
      tabulate::Table::Row_t row;
      for (const auto &cell : rows)
        if (std::holds_alternative<double>(cell)) {
          if (type == "miss_ratio")
            row.emplace_back(std::format("{:.6f}%", std::get<double>(cell) * 100));
          else
            row.emplace_back(std::format("{:.6f}MOps", 1.0 / std::get<double>(cell) / 1'000'000));
        } else {
          row.emplace_back(std::get<std::string>(cell));
        }
      table.add_row(row);
    }
    table.format()
        .font_align(tabulate::FontAlign::right)
        .corner(" ")
        .border_top(" ")
        .border_bottom(" ")
        .border_left(" ")
        .border_right(" ");
    table[1].format().corner("-").border_top("-");
    std::ostringstream oss;
    oss << table;
    std::istringstream iss{oss.str()};
    std::string output;
    std::string line;
    while (std::getline(iss, line))
      if (line.find_first_not_of(' ') != std::string::npos)
        output += line + "\n";
    std::println("{}", output);
  }

  // Write results to CSV
  if (!output_path.empty()) {
    std::ofstream output_file(output_path);
    if (!output_file.is_open())
      throw std::runtime_error("Failed to open output file: " + output_path);
    std::println(output_file, "{}",
                 "type,alpha," + fplus::join_elem(',', enabled_benchmark_names()));
    for (const auto &[type, rows] : results)
      for (const auto &row : rows)
        std::println(output_file, "{},{}", type,
                     fplus::join_elem(',', fplus::transform(
                                               [](const auto &v) {
                                                 return std::holds_alternative<double>(v)
                                                            ? std::format("{}", std::get<double>(v))
                                                            : std::get<std::string>(v);
                                               },
                                               row)));
    output_file.close();
  }
}

BENCHMARK("hm") {
  argparse::ArgumentParser program;
  program.add_argument("trace_path").help("The path to the cache trace file");
  program.add_argument("cache_size_ratio")
      .help("The ratio of the cache size to the number of unique objects in the trace")
      .scan<'g', double>();
  program.add_argument("top_k").help("The number of top results to return").scan<'u', size_t>();
  program.add_argument("alphas").help(
      "Comma-separated list of alpha values to use (e.g., '0.1,0.2,0.3')");
  program.add_argument("-p", "--parallel")
      .help("Run all experiments in parallel")
      .default_value(DEFAULT_PARALLEL)
      .implicit_value(true);
  program.add_argument("-o", "--output").help("Output file path (as CSV)").default_value("");

  std::string trace_path;
  double cache_size_ratio;
  size_t top_k;
  std::vector<std::string> alphas;
  std::string output_path;
  try {
    program.parse_args(argc, argv);
    trace_path = program.get<decltype(trace_path)>("trace_path");
    cache_size_ratio = program.get<decltype(cache_size_ratio)>("cache_size_ratio");
    top_k = program.get<decltype(top_k)>("top_k");
    options.parallel = program.get<bool>("--parallel");
    alphas = fplus::split(',', false, program.get<std::string>("alphas"));
    output_path = program.get<decltype(output_path)>("--output");
  } catch (const std::exception &e) {
    throw usage_error(program.help().str(), e.what());
  }

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
    const double coverage = results[0];
    const double update_time_avg_seconds = results[1];
    const double estimate_time_avg_seconds = results[2];

    coverages[alpha][std::string(name)] = coverage;
    update_avg_times[alpha][std::string(name)] = update_time_avg_seconds;
    estimate_avg_times[alpha][std::string(name)] = estimate_time_avg_seconds;
    spdlog::info(
        "[α={}] {}: (Coverage) {:.6f}%, (Update) {:.6f}MOps, (Estimate) {:.6f}MOps ({:.6f}s "
        "elapsed)",
        fplus::trim_right('.', fplus::trim_right('0', std::format("{:f}", std::stod(alpha)))), name,
        coverage * 100, 1.0 / update_time_avg_seconds / 1'000'000,
        1.0 / estimate_time_avg_seconds / 1'000'000, time_spent);
  });

  if (options.parallel) {
    for (const auto &alpha : alphas) {
      spdlog::info("Running H&M Trending (k={}) benchmark with α={}...", top_k, alpha);

      coverages.clear(); // Clear previous results
      benchmark_all(trace_path, cache_size, top_k, alpha);
    }

    wait();
    std::println();

    for (const auto &alpha : alphas) {
      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(coverages[alpha].begin(),
                                                                          coverages[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::println();
    }
  } else {
    for (const auto &alpha : alphas) {
      spdlog::info("Running H&M Trending (k={}) benchmark with α={}...", top_k, alpha);

      coverages.clear(); // Clear previous results
      benchmark_all(trace_path, cache_size, top_k, alpha);
      wait();
      std::println();

      // Sort by miss ratio (ascending)
      std::vector<std::pair<std::string_view, double>> miss_ratios_sorted(coverages[alpha].begin(),
                                                                          coverages[alpha].end());
      std::ranges::sort(miss_ratios_sorted,
                        [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
      spdlog::info("[α={}] Sorted by miss ratio (ascending):", alpha);
      for (const auto &[name, miss_ratio] : miss_ratios_sorted)
        spdlog::info("[α={}] {}: {:.6f}%", alpha, name, miss_ratio * 100);
      std::println();
    }
  }

  std::vector<std::tuple<std::string, std::string,
                         std::unordered_map<std::string, std::unordered_map<std::string, double>>>>
      result_maps = {
          {"coverage", "Trending Coverages", coverages},
          {"update_avg_time_s", "Average Update Time by Seconds", update_avg_times},
          {"estimate_avg_time_s", "Average Estimate Time by Seconds", estimate_avg_times},
      };

  std::unordered_map<std::string, std::vector<std::vector<std::variant<double, std::string>>>>
      results;
  for (const auto &[type, _, map] : result_maps) {
    std::vector<std::vector<std::variant<double, std::string>>> result;
    for (const auto &alpha : alphas) {
      std::vector<std::variant<double, std::string>> row;
      row.emplace_back(alpha);
      for (const auto &name : enabled_benchmark_names()) {
        if (const auto it = map.find(alpha); it != map.end())
          if (const auto it2 = it->second.find(name); it2 != it->second.end())
            row.emplace_back(it2->second);
          else
            row.emplace_back("N/A"); // If the benchmark was not run
        else
          row.emplace_back("N/A"); // If no results for this alpha
      }
      result.emplace_back(std::move(row));
    }
    results[type] = result;
  }

  // Print results
  for (const auto &[type, desc, _] : result_maps) {
    std::println("{}{}:", type == std::get<0>(result_maps[0]) ? "" : "\n", desc);
    tabulate::Table table;
    tabulate::Table::Row_t header{"Alpha"};
    for (const auto &name : enabled_benchmark_names())
      header.emplace_back(name);
    table.add_row(header);
    for (const auto &rows : results[type]) {
      tabulate::Table::Row_t row;
      for (const auto &cell : rows)
        if (std::holds_alternative<double>(cell)) {
          if (type == "miss_ratio")
            row.emplace_back(std::format("{:.6f}%", std::get<double>(cell) * 100));
          else
            row.emplace_back(std::format("{:.6f}MOps", 1.0 / std::get<double>(cell) / 1'000'000));
        } else {
          row.emplace_back(std::get<std::string>(cell));
        }
      table.add_row(row);
    }
    table.format()
        .font_align(tabulate::FontAlign::right)
        .corner(" ")
        .border_top(" ")
        .border_bottom(" ")
        .border_left(" ")
        .border_right(" ");
    table[1].format().corner("-").border_top("-");
    std::ostringstream oss;
    oss << table;
    std::istringstream iss{oss.str()};
    std::string output;
    std::string line;
    while (std::getline(iss, line))
      if (line.find_first_not_of(' ') != std::string::npos)
        output += line + "\n";
    std::println("{}", output);
  }

  // Write results to CSV
  if (!output_path.empty()) {
    std::ofstream output_file(output_path);
    if (!output_file.is_open())
      throw std::runtime_error("Failed to open output file: " + output_path);
    std::println(output_file, "{}",
                 "type,alpha," + fplus::join_elem(',', enabled_benchmark_names()));
    for (const auto &[type, rows] : results)
      for (const auto &row : rows)
        std::println(output_file, "{},{}", type,
                     fplus::join_elem(',', fplus::transform(
                                               [](const auto &v) {
                                                 return std::holds_alternative<double>(v)
                                                            ? std::format("{}", std::get<double>(v))
                                                            : std::get<std::string>(v);
                                               },
                                               row)));
    output_file.close();
  }
}

/********
 * Main *
 ********/
BENCHMARK_MAIN() {
  // Change "info" to "debug" to see more detailed logs
  spdlog::set_level(spdlog::level::info);

  return 0;
}
