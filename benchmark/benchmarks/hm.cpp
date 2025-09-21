#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <argparse/argparse.hpp>

#include "../../src/adapters/EpsilonGreedyAdapter.hpp"
#include "../../src/sketch.hpp"
#include "../baselines/AdaSketch.hpp"
#include "../baselines/CountMinSketch.hpp"
#include "../hm/reader.hpp"
#include "../utils/benchmark_task.hpp"
#include "../utils/errors.hpp"
#include "../utils/sketch.hpp"

using T = uint32_t;

struct Args {
  std::string trace_path;
  size_t cache_size;
  size_t top_k;
  size_t adapt_interval;
  double alpha;
  bool progress;
  std::string trace;
};

template <typename Freq> struct FreqCompare {
  auto operator()(const std::pair</* product_code */ uint32_t, /* freq */ Freq> &a,
                  const std::pair</* product_code */ uint32_t, /* freq */ Freq> &b) const -> bool {
    if (a.second != b.second)
      return a.second > b.second;
    return a.first < b.first;
  }
};

auto parse_args(int argc, char **argv) -> Args {
  argparse::ArgumentParser program;
  program.add_argument("trace_path").help("The path to the cache trace file");
  program.add_argument("cache_size").help("The cache size").scan<'u', size_t>();
  program.add_argument("top_k").help("The top-k value for the benchmark").scan<'u', size_t>();
  program.add_argument("adapt_interval")
      .help("The interval of adaptation (only used by EvolvingSketch)")
      .scan<'u', size_t>();
  program.add_argument("alpha")
      .help("The initial alpha value for time-decaying sketches")
      .scan<'g', double>();
  program.add_argument("-p", "--progress").help("Show progress bar").flag();
  program.add_argument("--trace")
      .help("The path to a CSV file where the objective history is saved at each adapt_interval. "
            "For EVO, an additional 'parameter' (i.e., alpha) column is included.")
      .default_value("");

  try {
    program.parse_args(argc, argv);
    return {
        .trace_path = program.get<std::string>("trace_path"),
        .cache_size = program.get<size_t>("cache_size"),
        .top_k = program.get<size_t>("top_k"),
        .adapt_interval = program.get<size_t>("adapt_interval"),
        .alpha = program.get<double>("alpha"),
        .progress = program.get<bool>("--progress"),
        .trace = program.get<std::string>("--trace"),
    };
  } catch (const std::exception &e) {
    throw usage_error(program.help().str(), e.what());
  }
}

struct Noop0 {
  void operator()(size_t rank) const noexcept {}
};

template <typename Sketch, typename OnHit = Noop0>
  requires std::is_invocable_r_v<void, OnHit, size_t>
auto benchmark(Sketch &sketch, const Args &args, OnHit on_hit = Noop0{}) -> double {
  using Freq = decltype(sketch.estimate(0));

  double dcg = 0;

  const TransactionTrace trace(args.trace_path);

  size_t progress = 0;

  std::set<std::pair</* product_code */ uint32_t, /* freq */ Freq>, FreqCompare<Freq>> top_k;
  std::unordered_map</* product_code */ uint32_t, /* freq */ Freq> product_code2freq_in_top_k;

  if (args.trace.empty()) {
    for (const auto &trans : trace) {
      const uint32_t product = trans.product_code;

      if (product_code2freq_in_top_k.contains(product)) {
        size_t rank = std::distance(top_k.begin(),
                                    top_k.find({product, product_code2freq_in_top_k[product]})) +
                      1;
        dcg += 1.0 / std::log2(rank + 1);
        if constexpr (!std::same_as<OnHit, Noop0>)
          on_hit(rank);
        sketch.update(product);
        top_k.erase({product, product_code2freq_in_top_k[product]});
        const auto freq = sketch.estimate(product);
        product_code2freq_in_top_k[product] = freq;
        top_k.emplace(product, freq);

        if (args.progress && progress++ % 1000 == 0)
          std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                                  static_cast<double>(trace.size()) * 100)
                    << "\r" << std::flush;

        continue;
      }

      sketch.update(product);
      const auto freq = sketch.estimate(product);

      if (top_k.size() < args.top_k) {
        top_k.emplace(product, freq);
        product_code2freq_in_top_k[product] = freq;

        if (args.progress && progress++ % 1000 == 0)
          std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                                  static_cast<double>(trace.size()) * 100)
                    << "\r" << std::flush;

        continue;
      }

      // Try swapping out the smallest element in the set
      size_t tries = 0; // Avoid too many iterations
      while (freq > top_k.rbegin()->second && tries++ < args.top_k) {
        const auto [popped_product_code, popped_stale_freq] = *top_k.rbegin();
        top_k.erase({popped_product_code, popped_stale_freq});

        const auto latest_freq = sketch.estimate(popped_product_code);

        if (latest_freq >= freq) {
          top_k.emplace(popped_product_code, latest_freq);
          product_code2freq_in_top_k[popped_product_code] = latest_freq;
        } else {
          top_k.emplace(product, freq);
          product_code2freq_in_top_k.erase(popped_product_code);
          product_code2freq_in_top_k[product] = freq;
          break;
        }
      }

      if (args.progress && progress++ % 1000 == 0)
        std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                                static_cast<double>(trace.size()) * 100)
                  << "\r" << std::flush;

      // // Print top 50
      // if (args.progress && progress++ % 1'000'000 == 0) {
      //   for (size_t i = 0; i < 50 && i < top_k.size(); i++) {
      //     const auto &[product, freq] = *std::next(top_k.begin(), i);
      //     std::println("{}: product={}, freq={}", i, product, freq);
      //   }
      //   std::println();
      // }
    }
  } else {
    double dcg_curr = 0;
    std::vector<double> history;

    for (const auto &trans : trace) {
      const uint32_t product = trans.product_code;

      if (product_code2freq_in_top_k.contains(product)) {
        size_t rank = std::distance(top_k.begin(),
                                    top_k.find({product, product_code2freq_in_top_k[product]})) +
                      1;
        dcg += 1.0 / std::log2(rank + 1);
        dcg_curr += 1.0 / std::log2(rank + 1);
        if constexpr (!std::same_as<OnHit, Noop0>)
          on_hit(rank);
        sketch.update(product);
        top_k.erase({product, product_code2freq_in_top_k[product]});
        const auto freq = sketch.estimate(product);
        product_code2freq_in_top_k[product] = freq;
        top_k.emplace(product, freq);

        progress++;

        if (progress % args.adapt_interval == 0) {
          history.push_back(dcg_curr);
          dcg_curr = 0;
        }

        if (args.progress && progress % 1000 == 0)
          std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                                  static_cast<double>(trace.size()) * 100)
                    << "\r" << std::flush;

        continue;
      }

      sketch.update(product);
      const auto freq = sketch.estimate(product);

      if (top_k.size() < args.top_k) {
        top_k.emplace(product, freq);
        product_code2freq_in_top_k[product] = freq;

        progress++;

        if (progress % args.adapt_interval == 0) {
          history.push_back(dcg_curr);
          dcg_curr = 0;
        }

        if (args.progress && progress % 1000 == 0)
          std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                                  static_cast<double>(trace.size()) * 100)
                    << "\r" << std::flush;

        continue;
      }

      // Try swapping out the smallest element in the set
      size_t tries = 0; // Avoid too many iterations
      while (freq > top_k.rbegin()->second && tries++ < args.top_k) {
        const auto [popped_product_code, popped_stale_freq] = *top_k.rbegin();
        top_k.erase({popped_product_code, popped_stale_freq});

        const auto latest_freq = sketch.estimate(popped_product_code);

        if (latest_freq >= freq) {
          top_k.emplace(popped_product_code, latest_freq);
          product_code2freq_in_top_k[popped_product_code] = latest_freq;
        } else {
          top_k.emplace(product, freq);
          product_code2freq_in_top_k.erase(popped_product_code);
          product_code2freq_in_top_k[product] = freq;
          break;
        }
      }

      progress++;

      if (progress % args.adapt_interval == 0) {
        history.push_back(dcg_curr);
        dcg_curr = 0;
      }

      if (args.progress && progress % 1000 == 0)
        std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                                static_cast<double>(trace.size()) * 100)
                  << "\r" << std::flush;

      // // Print top 50
      // if (args.progress && progress++ % 1'000'000 == 0) {
      //   for (size_t i = 0; i < 50 && i < top_k.size(); i++) {
      //     const auto &[product, freq] = *std::next(top_k.begin(), i);
      //     std::println("{}: product={}, freq={}", i, product, freq);
      //   }
      //   std::println();
      // }
    }

    std::ofstream file(args.trace);
    if (!file.is_open())
      throw std::runtime_error("Failed to open file for writing trace history: " + args.trace);

    file << "objective\n";
    // Auto burn-in: estimate number of initial intervals to drop where the top-k list is still
    // under-filled and the catalog coverage is tiny. Heuristic based on cache/top_k and
    // unique_products: skip intervals until both conditions likely satisfied.
    // We approximate via a simple function of counts available at this point.
    size_t burn = 0;
    // Target: top_k at least 90% filled and cumulative distinct >= 10% of cache_size.
    // Since we don't persist per-interval metadata here, we approximate with:
    // burn_intervals ≈ max(ceil(0.1 * args.cache_size / args.top_k), 5)
    // and also a floor based on unique-products ratio.
    const auto approx1 = static_cast<size_t>(
        std::ceil(0.1 * static_cast<double>(args.cache_size) / static_cast<double>(args.top_k)));
    const size_t approx_min = 5;
    burn = std::max(approx1, approx_min);
    // Also scale by unique_products/cache_size ratio to avoid excessive burn on tiny traces
    const TransactionTrace trace(args.trace_path);
    const size_t unique_products = count_unique_products(trace);
    if (unique_products > 0) {
      const double ratio = static_cast<double>(unique_products) /
                           std::max(1.0, static_cast<double>(args.cache_size));
      if (ratio < 5.0) {
        const auto up = static_cast<double>(unique_products);
        const auto tk = std::max(1.0, static_cast<double>(args.top_k));
        burn = std::max(burn, static_cast<size_t>(std::ceil(0.02 * up / tk)));
      }
    }
    burn = std::min(burn, history.size());
    for (size_t i = burn; i < history.size(); ++i)
      file << std::format("{}\n", history[i]);

    file.close();
  }

  return dcg;
}

auto f(const uint32_t t, const double alpha) -> float {
  return static_cast<float>(std::exp(alpha * static_cast<double>(t) / 10000.0));
}

REGISTER_BENCHMARK_TASK("CMS") {
  const Args args = parse_args(argc, argv);
  CountMinSketch<T> sketch(args.cache_size);
  const double dcg = benchmark(sketch, args);
  return std::vector{dcg, sketch.update_time_avg_seconds(), sketch.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("ADA") {
  const Args args = parse_args(argc, argv);
  auto f2 = [alpha = args.alpha](uint32_t t) -> float { return f(t, alpha); };
  AdaSketch<T, decltype(f2)> sketch(args.cache_size, AdaSketchOptions<decltype(f2)>{.f = f2});
  const double dcg = benchmark(sketch, args);
  return std::vector{dcg, sketch.update_time_avg_seconds(), sketch.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("EVO_PRUNING_ONLY") {
  const Args args = parse_args(argc, argv);
  auto f2 = [](uint32_t t, double alpha) -> float { return f(t, alpha); };
  EvolvingSketch<T, decltype(f2)> sketch(args.cache_size, {.initial_alpha = args.alpha, .f = f2});
  const double dcg = benchmark(sketch, args);
  return std::vector{dcg, sketch.update_time_avg_seconds(), sketch.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("EVO") {
  const Args args = parse_args(argc, argv);

  // SlidingWindowThompsonSamplingAdapter adapter{0.01, 1000.0, 100, 10.0, 500};
  EpsilonGreedyAdapter adapter{0.01, 1000.0, 100, 0.1, 0.99};

  if (!args.trace.empty())
    adapter.start_recording_history();

  auto f2 = [](uint32_t t, double alpha) -> float { return f(t, alpha); };
  EvolvingSketchOptim<T, decltype(f2), double> sketch(
      args.cache_size, {.initial_alpha = args.alpha,
                        .f = f2,
                        .adapter = &adapter,
                        .adapt_interval = static_cast<uint32_t>(args.adapt_interval)});

  Args benchmark_args = args;
  benchmark_args.trace = ""; // Disable internal trace recording
  const double dcg = benchmark(sketch, benchmark_args,
                               [&](size_t rank) { sketch.sum += 1.0 / std::log2(rank + 1); });

  if (!args.trace.empty())
    adapter.save_history(std::filesystem::path{args.trace});

  return std::vector{dcg, sketch.update_time_avg_seconds(), sketch.estimate_time_avg_seconds()};
}

BENCHMARK_TASK_MAIN();
