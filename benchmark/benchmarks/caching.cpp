#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>

#include <fplus/fplus.hpp>
#include <vector>

#include "../../src/adapters/EpsilonGreedyAdapter.hpp"
#include "../../src/sketch.hpp"
#include "../baselines/AdaSketch.hpp"
#include "../baselines/CountMinSketch.hpp"
#include "../caching/FIFO.hpp"
#include "../caching/W-TinyLFU.hpp"
#include "../caching/policy.hpp"
#include "../caching/reader.hpp"
#include "../utils/benchmark_task.hpp"
#include "../utils/errors.hpp"
#include "../utils/sketch.hpp"

using K = uint64_t;
using V = uint64_t;

inline constexpr bool RECORD_HISTORY = false;

struct Args {
  std::string trace_path;
  size_t cache_size;
  double alpha;
};

auto parse_args(int argc, char **argv) -> Args {
  if (argc < 3)
    throw usage_error("<trace_path> <cache_size> <alpha>");

  const std::string trace_path = argv[0];

  const size_t cache_size = std::stoul(argv[1]);
  if (cache_size == 0)
    throw usage_error("Cache size must be greater than 0");

  const double alpha = std::stod(argv[2]);

  return {
      .trace_path = trace_path,
      .cache_size = cache_size,
      .alpha = alpha,
  };
}

auto benchmark(
    CacheReplacementPolicy<K, V> &policy, const std::string &trace_path, const size_t cache_size,
    const std::function<void()> &&on_hit = []() {}) -> double {
  size_t hit_count = 0;

  const CachingTrace trace(trace_path);
  MockCache<K, V> cache(cache_size);

  for (const auto &req : trace) {
    V value; // This is a dummy value
    if (cache.contains(req.obj_id)) {
      hit_count++;
      on_hit();
      policy.handle_cache_hit(req.obj_id);
    } else {
      policy.handle_cache_miss(cache, req.obj_id, value);
    }
  }

  return static_cast<double>(trace.size() - hit_count) / static_cast<double>(trace.size());
}

REGISTER_BENCHMARK_TASK("FIFO") {
  const Args args = parse_args(argc, argv);
  FIFOPolicy<K, V> policy(args.cache_size);
  return benchmark(policy, args.trace_path, args.cache_size);
}

auto f(const uint32_t t, const double alpha) -> float {
  return static_cast<float>(std::exp(alpha * static_cast<double>(t) / 10000.0));
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_CMS") {
  const Args args = parse_args(argc, argv);
  WTinyLFUPolicy<K, V, CountMinSketch<K>> policy(args.cache_size,
                                                 CountMinSketch<K>(args.cache_size));
  const double miss_ratio = benchmark(policy, args.trace_path, args.cache_size);
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_ADA") {
  const Args args = parse_args(argc, argv);
  WTinyLFUPolicy<K, V, AdaSketch<K>> policy(
      args.cache_size, AdaSketch<K>(args.cache_size, {.f = [alpha = args.alpha](const auto t) {
                                      return f(t, alpha);
                                    }}));
  const double miss_ratio = benchmark(policy, args.trace_path, args.cache_size);
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_EVO_TUNING_ONLY") {
  const Args args = parse_args(argc, argv);
  WTinyLFUPolicy<K, V, EvolvingSketch<K>> policy(
      args.cache_size,
      EvolvingSketch<K>(args.cache_size,
                        {.initial_alpha = args.alpha, .f = f, .tuning_interval = 200}));
  const double miss_ratio = benchmark(policy, args.trace_path, args.cache_size);
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_EVO") {
  const Args args = parse_args(argc, argv);

  EpsilonGreedyAdapter adapter(0.1, 1000.0, 100, 0.1, 0.99);
  constexpr uint32_t ADAPT_INTERVAL = 100000;

  if constexpr (RECORD_HISTORY)
    adapter.start_recording_history();

  EvolvingSketchOptim<K> sketch(args.cache_size, {.initial_alpha = args.alpha,
                                                  .f = f,
                                                  .tuning_interval = 200,
                                                  .adapter = &adapter,
                                                  .adapt_interval = ADAPT_INTERVAL});
  WTinyLFUPolicy<K, V, EvolvingSketchOptim<K>> policy(args.cache_size, sketch);

  const double miss_ratio =
      benchmark(policy, args.trace_path, args.cache_size, [&]() { sketch.hit_count++; });
  if constexpr (RECORD_HISTORY)
    adapter.save_history(std::format(
        "output/{}.alpha_{}.trace.csv",
        std::filesystem::path(args.trace_path).replace_extension().filename().string(),
        fplus::trim_right('.', fplus::trim_right('0', std::format("{:f}", args.alpha)))));
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

BENCHMARK_TASK_MAIN();
