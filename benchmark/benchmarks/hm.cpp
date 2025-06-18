#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_set>

#include "../../src/adapters/SlidingWindowThompsonSamplingAdapter.hpp"
#include "../../src/sketch.hpp"
#include "../baselines/AdaSketch.hpp"
#include "../baselines/CountMinSketch.hpp"
#include "../hm/reader.hpp"
#include "../utils/benchmark_task.hpp"
#include "../utils/errors.hpp"
#include "../utils/sketch.hpp"

using T = uint32_t;

inline constexpr bool RECORD_HISTORY = false;

struct Args {
  std::string trace_path;
  size_t cache_size;
  size_t top_k;
  double alpha;
};

struct MinFreqCompare {
  auto operator()(std::pair<uint32_t, uint32_t> const &a,
                  std::pair<uint32_t, uint32_t> const &b) const -> bool {
    return a.second > b.second;
  }
};

auto parse_args(int argc, char **argv) -> Args {
  if (argc < 4)
    throw usage_error("<trace_path> <cache_size> <top_k> <alpha>");

  const std::string trace_path = argv[0];

  const size_t cache_size = std::stoul(argv[1]);
  if (cache_size == 0)
    throw usage_error("Cache size must be greater than 0");

  const size_t top_k = std::stoul(argv[2]);

  const double alpha = std::stod(argv[3]);

  return {
      .trace_path = trace_path,
      .cache_size = cache_size,
      .top_k = top_k,
      .alpha = alpha,
  };
}

template <typename SKETCH>
auto benchmark(
    SKETCH &sketch, const std::string &trace_path, const size_t top_k,
    const std::function<void()> &&on_hit = []() {}) -> double {
  size_t hit_count = 0;

  const TransactionTrace trace(trace_path);

  std::unordered_set<uint32_t> top_k_set;
  std::priority_queue<std::pair<uint32_t, uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>,
                      MinFreqCompare>
      heap;

  for (const auto &trans : trace) {
    const uint32_t product = trans.product_code;

    if (top_k_set.contains(product)) {
      hit_count++;
      on_hit();
      sketch.update(product);
      continue;
    }

    sketch.update(product);
    const double freq = sketch.estimate(product);

    if (heap.size() < top_k) {
      heap.emplace(product, freq);
      top_k_set.insert(product);
      continue;
    }

    // if (freq > heap.top().second) {
    //   auto [poppedIP, _] = heap.top();
    //   heap.pop();
    //   heap.emplace(ip, freq);
    //   top_k_set.erase(poppedIP);
    //   top_k_set.insert(ip);
    // }

    // Try swapping out the smallest element in the heap
    size_t tries = 0; // Avoid too many iterations
    while (freq > heap.top().second && tries++ < top_k) {
      auto [popped_product_code, _] = heap.top();
      heap.pop();

      const double latest_freq = sketch.estimate(popped_product_code);

      if (latest_freq >= freq) {
        heap.emplace(popped_product_code, latest_freq);
      } else {
        heap.emplace(product, freq);
        top_k_set.erase(popped_product_code);
        top_k_set.insert(product);
        break;
      }
    }
  }

  return static_cast<double>(hit_count) / static_cast<double>(trace.size());
}

auto f(const uint32_t t, const double alpha) -> float {
  return static_cast<float>(std::exp(alpha * static_cast<double>(t) / 10000.0));
}

REGISTER_BENCHMARK_TASK("CMS") {
  const Args args = parse_args(argc, argv);
  CountMinSketch<T> sketch(args.cache_size);
  const double coverage = benchmark(sketch, args.trace_path, args.top_k);
  return std::vector{coverage, sketch.update_time_avg_seconds(),
                     sketch.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("ADA") {
  const Args args = parse_args(argc, argv);
  AdaSketch<T> sketch(args.cache_size,
                      {.f = [alpha = args.alpha](const auto t) { return f(t, alpha); }});
  const double coverage = benchmark(sketch, args.trace_path, args.top_k);
  return std::vector{coverage, sketch.update_time_avg_seconds(),
                     sketch.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("EVO_TUNING_ONLY") {
  const Args args = parse_args(argc, argv);
  EvolvingSketch<T> sketch(args.cache_size,
                           {.initial_alpha = args.alpha, .f = f, .tuning_interval = 10});
  const double coverage = benchmark(sketch, args.trace_path, args.top_k);
  return std::vector{coverage, sketch.update_time_avg_seconds(),
                     sketch.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("EVO") {
  const Args args = parse_args(argc, argv);

  SlidingWindowThompsonSamplingAdapter adapter(0.1, 10000.0, 100, 10.0, 500);
  constexpr uint32_t ADAPT_INTERVAL = 100;

  if constexpr (RECORD_HISTORY)
    adapter.start_recording_history();

  EvolvingSketchOptim<T> sketch(args.cache_size, {.initial_alpha = args.alpha,
                                                  .f = f,
                                                  .tuning_interval = 10,
                                                  .adapter = &adapter,
                                                  .adapt_interval = ADAPT_INTERVAL});

  const double coverage =
      benchmark(sketch, args.trace_path, args.top_k, [&]() { sketch.hit_count++; });
  if constexpr (RECORD_HISTORY)
    adapter.save_history(std::format(
        "output/{}.alpha_{}.trace.csv",
        std::filesystem::path(args.trace_path).replace_extension().filename().string(),
        fplus::trim_right('.', fplus::trim_right('0', std::format("{:f}", args.alpha)))));
  return std::vector{coverage, sketch.update_time_avg_seconds(),
                     sketch.estimate_time_avg_seconds()};
}

BENCHMARK_TASK_MAIN();
