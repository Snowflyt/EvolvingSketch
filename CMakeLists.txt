cmake_minimum_required(VERSION 3.28)

# Set the project name
project(EvolvingSketch)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable ASan to detect memory errors on *nix
if(CMAKE_BUILD_TYPE MATCHES Debug AND UNIX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
endif()

# Enable AVX2
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT APPLE)
  add_compile_options(-mavx2)
elseif(MSVC)
  add_compile_options(/arch:AVX2)
endif()

# Use libc++ when using Clang on *nix
if(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
endif()

# Include macOS SDK path if on Apple platform
if(APPLE)
  execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE MACOS_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CMAKE_OSX_SYSROOT "${MACOS_SDK_PATH}" CACHE STRING "" FORCE)
endif()

# Disable secure warnings on MSVC
if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Export compilation database
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add sources
set(projects)
file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h" "src/*.cpp" "src/*.hpp" "src/*.tpp" "src/*.cppm")
list(FILTER SOURCES EXCLUDE REGEX ".*[/\\\\]\\.[^/\\\\]+[/\\\\].*") # Filter out dirs starting with '.'
file(GLOB_RECURSE BENCHMARK_IMPL_SOURCES "benchmark/impl/*.h" "benchmark/impl/*.c" "benchmark/impl/*.hpp" "benchmark/impl/*.cpp" "benchmark/impl/*.tpp" "benchmark/impl/*.cppm")
list(FILTER BENCHMARK_IMPL_SOURCES EXCLUDE REGEX ".*[/\\\\]\\.[^/\\\\]+[/\\\\].*") # Filter out dirs starting with '.'

# Add CPM.cmake
include(cmake/CPM.cmake)

# Testing
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  file(GLOB_RECURSE TEST_SOURCES "test/*.cpp")

 CPMAddPackage("gh:doctest/doctest@2.4.12")

  # Generate a translation unit that implements doctest main
  set(DOCTEST_MAIN_CPP "${CMAKE_CURRENT_BINARY_DIR}/doctest_main.cpp")
  file(GENERATE
    OUTPUT "${DOCTEST_MAIN_CPP}"
    CONTENT "#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include <doctest/doctest.h>\n"
  )

  add_executable(tests ${SOURCES} ${TEST_SOURCES} "${DOCTEST_MAIN_CPP}")
  target_link_libraries(tests PRIVATE doctest::doctest)

  include(CTest)

  list(APPEND projects tests)
endif()

# Benchmark
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  file(GLOB_RECURSE BENCHMARK_SOURCES "benchmark/*.h" "benchmark/*.c" "benchmark/*.hpp" "benchmark/*.cpp" "benchmark/*.tpp" "benchmark/*.cppm")
  list(REMOVE_ITEM BENCHMARK_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/benchmark.cpp)
  list(FILTER BENCHMARK_SOURCES EXCLUDE REGEX "benchmark/benchmarks/.*")

  set(REPROC++ ON)
  CPMAddPackage("gh:DaanDeMeyer/reproc@14.2.5")

  file(GLOB BENCHMARK_TASKS "benchmark/benchmarks/*.cpp")

  foreach(BENCHMARK_TASK IN LISTS BENCHMARK_TASKS)
    get_filename_component(BENCHMARK_TASK_NAME ${BENCHMARK_TASK} NAME_WE)
    add_executable(benchmark_${BENCHMARK_TASK_NAME} ${BENCHMARK_TASK} ${SOURCES} ${BENCHMARK_SOURCES} ${SOURCES_WITHOUT_MAIN})
    list(APPEND projects benchmark_${BENCHMARK_TASK_NAME})
  endforeach()

  add_executable(benchmark ${SOURCES} ${BENCHMARK_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/benchmark.cpp)
  target_link_libraries(benchmark PRIVATE reproc++)
  list(APPEND projects benchmark)
endif()

# Use IWYU if found
find_program(IWYU_PATH NAMES include-what-you-use iwyu)

if(IWYU_PATH)
  message(STATUS "Found Include-What-You-Use (IWYU): ${IWYU_PATH}")
  set(IWYU_OPTIONS
    "-Xiwyu" "--mapping_file=${CMAKE_CURRENT_SOURCE_DIR}/iwyu.imp"
    "-Xiwyu" "--quoted_includes_first"
    "-Xiwyu" "--cxx17ns"
    "-w"
  )

  foreach(project IN LISTS projects)
    set_property(TARGET ${project} PROPERTY CXX_INCLUDE_WHAT_YOU_USE "${IWYU_PATH};${IWYU_OPTIONS}")
  endforeach()
endif()

# Packages
CPMAddPackage("gh:Dobiasd/FunctionalPlus@0.2.25")
CPMAddPackage("gh:Neargye/magic_enum@0.9.7")
CPMAddPackage("gh:gabime/spdlog@1.15.3")
CPMAddPackage("gh:p-ranav/argparse@3.2")
CPMAddPackage("gh:p-ranav/tabulate@1.5")
CPMAddPackage(
  NAME mio
  GIT_TAG 8b6b7d878c89e81614d05edca7936de41ccdd2da
  GITHUB_REPOSITORY vimpunk/mio
)

foreach(project IN LISTS projects)
  target_link_libraries(${project} PRIVATE FunctionalPlus::fplus)
  target_link_libraries(${project} PRIVATE magic_enum::magic_enum)
  target_link_libraries(${project} PRIVATE spdlog::spdlog)
  target_link_libraries(${project} PRIVATE mio::mio)
endforeach()
