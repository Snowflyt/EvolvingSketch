#pragma once

#include <format>
#include <iostream>
#include <print>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <fplus/fplus.hpp>
#include <spdlog/spdlog.h>

#include "errors.hpp"

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a##b

class BenchmarkTask {
public:
  static std::unordered_map<std::string, BenchmarkTask *> tasks;
  static std::vector<std::string> task_names;

  explicit BenchmarkTask(const std::string &name) {
    tasks[name] = this;
    task_names.push_back(name);
  }

  virtual ~BenchmarkTask() = default;

  BenchmarkTask(const BenchmarkTask &) = delete;
  auto operator=(const BenchmarkTask &) -> BenchmarkTask & = delete;
  BenchmarkTask(BenchmarkTask &&) = delete;
  auto operator=(BenchmarkTask &&) -> BenchmarkTask & = delete;

  virtual auto run(int argc, char **argv) -> std::variant<double, std::vector<double>> = 0;
};

inline std::unordered_map<std::string, BenchmarkTask *> BenchmarkTask::tasks;
inline std::vector<std::string> BenchmarkTask::task_names;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define REGISTER_BENCHMARK_TASK(task_name)                                                         \
  class CONCAT(BenchmarkTask, __LINE__) : public BenchmarkTask {                                   \
  public:                                                                                          \
    CONCAT(BenchmarkTask, __LINE__)() : BenchmarkTask(task_name) {}                                \
    auto run(int argc, char **argv) -> std::variant<double, std::vector<double>> override;         \
    static constexpr auto name = task_name;                                                        \
  };                                                                                               \
  inline CONCAT(BenchmarkTask, __LINE__) CONCAT(benchmark_task_, __LINE__);                        \
  auto CONCAT(BenchmarkTask, __LINE__)::run(int argc, char **argv)                                 \
      -> std::variant<double, std::vector<double>>

inline auto benchmark_task_main(int argc, char **argv) -> int {
  if (argc < 2) {
    std::println(std::cerr, "Usage: {} {{{}}} ...", argv[0],
                 fplus::join_elem('|', BenchmarkTask::task_names));
    return 1;
  }

  const std::string name = argv[1];

  if (const auto it = BenchmarkTask::tasks.find(name); it == BenchmarkTask::tasks.end()) {
    std::println(std::cerr, "Unknown benchmark name: {}", name);
    std::println(std::cerr, "Usage: {} {{{}}} ...", argv[0],
                 fplus::join_elem('|', BenchmarkTask::task_names));
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
    const auto results = BenchmarkTask::tasks[name]->run(argc - 1, processed_argv);
    std::println("{}",
                 std::holds_alternative<double>(results)
                     ? std::format("{}", std::get<double>(results))
                     : fplus::join_elem(
                           ',', fplus::transform([](const auto v) { return std::format("{}", v); },
                                                 std::get<std::vector<double>>(results))));
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
#define BENCHMARK_TASK_MAIN()                                                                      \
  auto main(int argc, char **argv) -> int { return benchmark_task_main(argc, argv); }              \
  auto benchmark_task_main(int argc, char **argv) -> int
