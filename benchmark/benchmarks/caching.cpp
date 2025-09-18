#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <argparse/argparse.hpp>
#include <fplus/fplus.hpp>

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

struct Args {
  std::string trace_path;
  size_t cache_size;
  size_t adapt_interval;
  double alpha;
  bool progress;
  bool record_adaptation_history;
};

auto parse_args(int argc, char **argv) -> Args {
  argparse::ArgumentParser program;
  program.add_argument("trace_path").help("The path to the cache trace file");
  program.add_argument("cache_size").help("The cache size").scan<'u', size_t>();
  program.add_argument("adapt_interval")
      .help("The interval of adaptation (only used by EvolvingSketch)")
      .scan<'u', size_t>();
  program.add_argument("alpha")
      .help("The initial alpha value for time-decaying sketches")
      .scan<'g', double>();
  program.add_argument("-p", "--progress").help("Show progress bar").flag();
  program.add_argument("--record-adaptation-history")
      .help("Record adaptation history for W-TinyLFU_EVO (does not affect other policies)")
      .flag();

  try {
    program.parse_args(argc, argv);
    return {
        .trace_path = program.get<std::string>("trace_path"),
        .cache_size = program.get<size_t>("cache_size"),
        .adapt_interval = program.get<size_t>("adapt_interval"),
        .alpha = program.get<double>("alpha"),
        .progress = program.get<bool>("--progress"),
        .record_adaptation_history = program.get<bool>("--record-adaptation-history"),
    };
  } catch (const std::exception &e) {
    throw usage_error(program.help().str(), e.what());
  }
}

struct Noop0 {
  void operator()() const noexcept {}
};

template <typename OnHit = Noop0>
  requires std::is_invocable_r_v<void, OnHit>
auto benchmark(CacheReplacementPolicy<K, V> &policy, const Args &args, OnHit on_hit = Noop0{})
    -> double {
  size_t hit_count = 0;

  const CachingTrace trace(args.trace_path);
  MockCache<K, V> cache(args.cache_size);

  size_t progress = 0;

  for (const auto &req : trace) {
    V value; // This is a dummy value
    if (cache.contains(req.obj_id)) {
      hit_count++;
      if constexpr (!std::same_as<OnHit, Noop0>)
        on_hit();
      policy.handle_cache_hit(req.obj_id);
    } else {
      policy.handle_cache_miss(cache, req.obj_id, value);
    }

    if (args.progress && progress++ % 1000 == 0)
      std::cout << std::format("{:.4f}%", static_cast<double>(progress) /
                                              static_cast<double>(trace.size()) * 100)
                << "\r" << std::flush;
  }

  return static_cast<double>(trace.size() - hit_count) / static_cast<double>(trace.size());
}

REGISTER_BENCHMARK_TASK("FIFO") {
  const Args args = parse_args(argc, argv);
  FIFOPolicy<K, V> policy(args.cache_size);
  return benchmark(policy, args);
}

auto f(const uint32_t t, const double alpha) -> float {
  return static_cast<float>(std::exp(alpha * static_cast<double>(t) / 10000.0));
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_CMS") {
  const Args args = parse_args(argc, argv);
  WTinyLFUPolicy<K, V, CountMinSketch<K>> policy{
      args.cache_size, std::make_shared<CountMinSketch<K>>(args.cache_size)};
  const double miss_ratio = benchmark(policy, args);
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_ADA") {
  const Args args = parse_args(argc, argv);
  auto f2 = [alpha = args.alpha](uint32_t t) -> float { return f(t, alpha); };
  WTinyLFUPolicy<K, V, AdaSketch<K, decltype(f2)>> policy{
      args.cache_size, std::make_shared<AdaSketch<K, decltype(f2)>>(
                           args.cache_size, AdaSketchOptions<decltype(f2)>{.f = f2})};
  const double miss_ratio = benchmark(policy, args);
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_EVO_PRUNING_ONLY") {
  const Args args = parse_args(argc, argv);
  auto f2 = [](uint32_t t, double alpha) -> float { return f(t, alpha); };
  WTinyLFUPolicy<K, V, EvolvingSketch<K, decltype(f2)>> policy{
      args.cache_size, std::make_shared<EvolvingSketch<K, decltype(f2)>>(
                           args.cache_size, EvolvingSketchOptions<decltype(f2)>{
                                                .initial_alpha = args.alpha, .f = f2})};
  const double miss_ratio = benchmark(policy, args);
  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

REGISTER_BENCHMARK_TASK("W-TinyLFU_EVO") {
  const Args args = parse_args(argc, argv);

  EpsilonGreedyAdapter adapter{0.01, 1000.0, 100, 0.1, 0.99};

  if (args.record_adaptation_history)
    adapter.start_recording_history();

  auto f2 = [](uint32_t t, double alpha) -> float { return f(t, alpha); };
  auto sketch = std::make_shared<EvolvingSketchOptim<K, decltype(f2)>>(
      args.cache_size,
      EvolvingSketchOptimOptions{.initial_alpha = args.alpha,
                                 .f = f2,
                                 .adapter = &adapter,
                                 .adapt_interval = static_cast<uint32_t>(args.adapt_interval)});
  WTinyLFUPolicy<K, V, EvolvingSketchOptim<K, decltype(f2)>> policy{args.cache_size, sketch};

  const double miss_ratio = benchmark(policy, args, [&]() { sketch->sum++; });

  if (args.record_adaptation_history)
    adapter.save_history(std::format(
        "output/{}.alpha_{}.trace.csv",
        std::filesystem::path(args.trace_path).replace_extension().filename().string(),
        fplus::trim_right('.', fplus::trim_right('0', std::format("{:f}", args.alpha)))));

  return std::vector{miss_ratio, policy.update_time_avg_seconds(),
                     policy.estimate_time_avg_seconds()};
}

BENCHMARK_TASK_MAIN();
