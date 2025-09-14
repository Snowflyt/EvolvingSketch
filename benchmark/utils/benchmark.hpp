#pragma once

#include <filesystem>
#include <fplus/split.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fplus/fplus.hpp>
#include <reproc++/run.hpp>
#include <spdlog/spdlog.h>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#if defined(__linux__)
#include <linux/limits.h> // for PATH_MAX
#elif defined(__APPLE__)
#include <mach-o/dyld.h> // for _NSGetExecutablePath
#endif
#elif defined(_WIN32)
#include <windows.h>
#endif

#include "../../src/utils/time.hpp"
#include "errors.hpp"

inline constexpr bool DEFAULT_PARALLEL = false;
// inline constexpr size_t DEFAULT_TIMEOUT_MILLISECONDS = 900'000UZ;
inline constexpr size_t DEFAULT_TIMEOUT_MILLISECONDS = std::numeric_limits<size_t>::max();

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a##b

template <typename T>
concept ConvertibleToString = std::is_integral_v<std::remove_cvref_t<T>> ||
                              std::is_floating_point_v<std::remove_cvref_t<T>> ||
                              std::is_same_v<std::remove_cvref_t<T>, std::string> ||
                              std::is_same_v<std::remove_cvref_t<T>, std::string_view> ||
                              std::is_same_v<std::remove_cvref_t<T>, const char *>;

auto convert_to_string(ConvertibleToString auto &&value) -> std::string {
  using T = std::decay_t<decltype(value)>;
  if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>)
    return std::to_string(value); // Handle integral types
  else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
                     std::is_same_v<T, const char *>)
    return std::string(value); // Handle std::string, std::string_view, const char*
  else
    static_assert(!std::is_same_v<T, T>, "Unsupported type for conversion to string");
}

struct BenchmarkOptions {
  bool parallel = DEFAULT_PARALLEL;
  size_t timeout_milliseconds = DEFAULT_TIMEOUT_MILLISECONDS;
};

class Benchmark {
public:
  static std::unordered_map<std::string, Benchmark *> benchmarks;
  static std::vector<std::string> benchmark_names;

  explicit Benchmark(const std::string &&name, const BenchmarkOptions &&opts)
      : name(name), filename_(name), options(opts),
        available_benchmark_names_(get_available_benchmarks()),
        enabled_benchmark_names_(available_benchmark_names_) {
    benchmarks[name] = this;
    benchmark_names.push_back(name);
  }

  virtual ~Benchmark() = default;

  Benchmark(const Benchmark &) = default;
  auto operator=(const Benchmark &) -> Benchmark & = default;
  Benchmark(Benchmark &&) = delete;
  auto operator=(Benchmark &&) -> Benchmark & = delete;

  virtual void run(int argc, char **argv) = 0;

  void on_benchmark_finished(
      std::function<void(const std::string_view name, const std::vector<std::string> &args,
                         const std::vector<double> &results, const double time_spent)>
          listener) {
    benchmark_finished_listeners_.emplace_back(std::move(listener));
  }

  void on_benchmark_finished(
      std::function<void(const std::string_view name, const std::vector<std::string> &args,
                         const std::vector<double> &results)>
          listener) {
    benchmark_finished_listeners_.emplace_back(
        [listener =
             std::move(listener)](const std::string_view name, const std::vector<std::string> &args,
                                  const std::vector<double> &results,
                                  const double /*time_spent*/) { listener(name, args, results); });
  }

  void wait() {
    for (auto &task : tasks_)
      task.wait();
  }

  template <ConvertibleToString... Args> void benchmark(const std::string &name, Args &&...args) {
    const std::vector<std::string> arguments{convert_to_string(std::forward<Args>(args))...};

    auto benchmark_func = [this, name = std::string(name), arguments = arguments]() {
      reproc::process process;

      reproc::options opts;
      opts.redirect.out.type = reproc::redirect::pipe;
      opts.redirect.err.type = reproc::redirect::pipe;

      std::vector<std::string> process_args{
          (executable_path().parent_path() / ("benchmark_" + filename_)).string(),
          std::string(name)};
      for (const std::string &argument : arguments)
        process_args.push_back(argument);

      std::string command;
      for (const std::string &argument : process_args)
        command += argument + " ";
      command.pop_back();
      spdlog::debug("[{}] Running benchmark with command: {}", name, command);

      const double start = get_current_time_in_seconds();

      std::error_code ec = process.start(process_args, opts);
      if (ec) {
        spdlog::error("[{}] Benchmark failed to start: {}", name,
                      fplus::trim_whitespace(ec.message()));
        return;
      }

      std::string output;

      reproc::sink::string sink(output);

      ec = reproc::drain(process, sink, sink);
      if (ec) {
        spdlog::error("[{}] Failed to read process output: {}", name,
                      fplus::trim_whitespace(ec.message()));
        return;
      }

      int status = 0;
      try {
        std::tie(status, ec) = process.wait(reproc::milliseconds{options.timeout_milliseconds});
      } catch (const std::exception &e) {
        spdlog::error("[{}] Failed to wait for process: {}", name,
                      fplus::trim_whitespace(std::string(e.what())));
        return;
      } catch (...) {
        spdlog::error("[{}] Failed to wait for process: unknown error", name);
        return;
      }
      if (ec) {
        spdlog::error("[{}] Failed to wait for process: {}", name,
                      fplus::trim_whitespace(ec.message()));
        return;
      }
      if (status) {
        spdlog::error("[{}] {}", name, fplus::trim_whitespace(output));
        spdlog::error("[{}] Process exited with status: {}", name, status);
        return;
      }

      std::vector<double> results = fplus::transform([](const auto v) { return std::stod(v); },
                                                     fplus::split(',', false, output));

      for (const auto &listener : benchmark_finished_listeners_)
        listener(name, fplus::drop(2, process_args), results,
                 get_current_time_in_seconds() - start);
    };

    if (options.parallel)
      tasks_.emplace_back(std::async(std::launch::async, benchmark_func));
    else
      benchmark_func();
  }

  template <ConvertibleToString... Args> void benchmark_all(Args &&...args) {
    for (const std::string &name : enabled_benchmark_names_)
      benchmark(name, std::forward<Args>(args)...);
  }

  void set_enabled_benchmarks(const std::vector<std::string> &enabled_benchmarks) {
    // Check if all enabled benchmarks are available
    for (const std::string &enabled_benchmark : enabled_benchmarks)
      if (std::ranges::find(available_benchmark_names_, enabled_benchmark) ==
          available_benchmark_names_.end())
        throw std::runtime_error("Unknown benchmark: " + enabled_benchmark);
    enabled_benchmark_names_ = enabled_benchmarks;
  }

protected:
  // NOLINTNEXTLINE
  std::string name;
  // NOLINTNEXTLINE
  BenchmarkOptions options;

  auto available_benchmark_names() -> std::vector<std::string> {
    return available_benchmark_names_;
  }

  auto enabled_benchmark_names() -> std::vector<std::string> { return enabled_benchmark_names_; }

private:
  std::string filename_;
  std::vector<std::string> available_benchmark_names_;
  std::vector<std::string> enabled_benchmark_names_;

  std::vector<std::future<void>> tasks_;

  std::vector<std::function<void(const std::string_view name, const std::vector<std::string> &args,
                                 const std::vector<double> &results, const double time_spent)>>
      benchmark_finished_listeners_;

  auto get_available_benchmarks() -> std::vector<std::string> {
    reproc::process process;

    reproc::options opts;
    opts.redirect.out.type = reproc::redirect::pipe;
    opts.redirect.err.type = reproc::redirect::pipe;

    std::vector<std::string> arguments{
        (executable_path().parent_path() / ("benchmark_" + filename_)).string()};
    std::error_code ec = process.start(arguments, opts);
    if (ec)
      throw std::runtime_error("Failed to start process: " + fplus::trim_whitespace(ec.message()));

    std::string output;

    reproc::sink::string sink(output);

    ec = reproc::drain(process, sink, sink);
    if (ec)
      throw std::runtime_error("Failed to read process output: " +
                               fplus::trim_whitespace(ec.message()));

    process.wait(reproc::infinite);

    if (!output.starts_with("Usage: "))
      throw std::runtime_error("Unexpected output from process: " + output);

    return fplus::fwd::apply(output, fplus::fwd::trim_whitespace(), fplus::fwd::split(' ', false),
                             fplus::fwd::elem_at_idx(2), fplus::fwd::drop(1),
                             fplus::fwd::drop_last(1), fplus::fwd::split('|', false));
  }

  static auto executable_path() -> std::filesystem::path {
#if defined(__linux__)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<char, PATH_MAX> buffer;
    auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length == -1)
      throw std::runtime_error("Failed to read symbolic link: " + std::string(strerror(errno)));
    return {buffer.data(), buffer.data() + length};
#elif defined(__APPLE__)
    uint32_t size = PATH_MAX;
    std::string buffer;
    buffer.resize(size);
    // first call: if buffer too small, size is set to required length
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
      buffer.resize(size);
      if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        throw std::runtime_error("Failed to get executable path");
    }
    // resolve any symlinks / relative parts
    return std::filesystem::canonical(buffer);
#elif defined(_WIN32)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<char, MAX_PATH> buffer;
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), buffer.size());
    if (length == 0)
      throw std::runtime_error("Failed to get module file name: " + std::to_string(GetLastError()));
    return {buffer.data(), buffer.data() + length};
#endif
  }
};

inline std::unordered_map<std::string, Benchmark *> Benchmark::benchmarks;
inline std::vector<std::string> Benchmark::benchmark_names;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BENCHMARK_WITHOUT_OPTIONS(name)                                                            \
  class CONCAT(Benchmark, __LINE__) : public Benchmark {                                           \
  public:                                                                                          \
    CONCAT(Benchmark, __LINE__)() : Benchmark(name, {}) {}                                         \
    void run(int argc, char **argv) override;                                                      \
  };                                                                                               \
  inline CONCAT(Benchmark, __LINE__) CONCAT(benchmark, __LINE__);                                  \
  void CONCAT(Benchmark, __LINE__)::run(int argc, char **argv)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BENCHMARK_WITH_OPTIONS(name, options)                                                      \
  class CONCAT(Benchmark, __LINE__) : public Benchmark {                                           \
  public:                                                                                          \
    CONCAT(Benchmark, __LINE__)() : Benchmark(name, options) {}                                    \
    void run(int argc, char **argv) override;                                                      \
  };                                                                                               \
  inline CONCAT(Benchmark, __LINE__) CONCAT(benchmark, __LINE__);                                  \
  void CONCAT(Benchmark, __LINE__)::run(int argc, char **argv)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BENCHMARK_MACRO_CHOOSER(...)                                                               \
  GET_3RD_ARG(__VA_ARGS__, BENCHMARK_WITH_OPTIONS, BENCHMARK_WITHOUT_OPTIONS, )

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BENCHMARK(...) BENCHMARK_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

inline auto benchmark_main(int argc, char **argv) -> int {
  if (argc < 2) {
    std::println("Usage:");
    for (const auto &name : Benchmark::benchmark_names) {
      try {
        Benchmark::benchmarks[name]->run(argc, argv);
      } catch (const usage_error &e) {
        std::println(std::cerr, "  {} {} {}", argv[0], name,
                     fplus::fwd::apply(e.usage(), fplus::fwd::trim_token_left(std::string("Usage")),
                                       fplus::fwd::trim_left(':'), fplus::fwd::trim_left(' '),
                                       fplus::fwd::trim_token_left(std::string(argv[0])),
                                       fplus::fwd::trim_left(' '),
                                       fplus::fwd::trim_token_left(name),
                                       fplus::fwd::trim_left(' '), fplus::fwd::split('\n', false),
                                       fplus::fwd::elem_at_idx(0)));
      }
    }
    return 1;
  }

  const std::string name = argv[1];

  if (const auto it = Benchmark::benchmarks.find(name); it == Benchmark::benchmarks.end()) {
    std::println(std::cerr, "Unknown benchmark name: {}", name);
    return 1;
  }

  char **processed_argv = new char *[argc - 1];
  processed_argv[0] = new char[std::strlen(argv[0]) + 1 + name.length() + /* null terminator */ 1];
  std::strcpy(processed_argv[0], argv[0]);
  std::strcat(processed_argv[0], " ");
  std::strcat(processed_argv[0], name.c_str());
  for (size_t i = 2; i < argc; ++i)
    processed_argv[i - 1] = argv[i];

  try {
    Benchmark::benchmarks[name]->run(argc - 1, processed_argv);
  } catch (const usage_error &e) {
    std::println(std::cerr, "Error: {}", e.msg());
    std::println(std::cerr, "\nUsage: {} {} {}", argv[0], name,
                 fplus::fwd::apply(e.usage(), fplus::fwd::trim_token_left(std::string("Usage")),
                                   fplus::fwd::trim_left(':'), fplus::fwd::trim_left(' '),
                                   fplus::fwd::trim_token_left(std::string(processed_argv[0])),
                                   fplus::fwd::trim_left(' ')));
    delete[] processed_argv[0];
    delete[] processed_argv;
    return 1;
  }

  delete[] processed_argv[0];
  delete[] processed_argv;
  return 0;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BENCHMARK_MAIN()                                                                           \
  auto benchmark_main_impl() -> int;                                                               \
  auto main(int argc, char **argv) -> int {                                                        \
    const int res = benchmark_main_impl();                                                         \
    if (res != 0)                                                                                  \
      return res;                                                                                  \
    return benchmark_main(argc, argv);                                                             \
  }                                                                                                \
  auto benchmark_main_impl() -> int
