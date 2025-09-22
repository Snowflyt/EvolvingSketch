# Evolving Sketch

This repository hosts the implementation and benchmarking of Evolving Sketch, an adaptively parameterized time-decaying frequency sketch for online streams.

## Software Architecture

- Programming Language: C++
- Build Tool: CMake
- Logging: spdlog
- Testing: doctest
- Others: Clang-Tidy / Clang-Format

## Build

This project uses CMake to build, and uses [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) to automatically download dependencies during the build, so you can build the project without worrying about dependencies.

Make sure your C++ compiler supports C++ 23 standard before building. This project has been tested with GCC 15 and Clang 20 on Linux.

To build the program in debug mode, **run the following command in the project root directory**:

```bash
mkdir -p build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --config Debug
cd ..
```

To build the program in release mode, **run the following command in the project root directory**:

```bash
mkdir -p build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
cd ..
```

## Data Retrieval

Several real-world datasets and a synthetic dataset are used in the benchmarking of Evolving Sketch. Follow the instructions below to retrieve and prepare the datasets.

Preprocessing scripts are implemented in TypeScript and are located in the `scripts/` directory, and you can run them with [Node.js](https://nodejs.org) ≥ 24. The following code snippets assume you **run commands in the project root directory**.

1. Cache traces. The cache traces we used are from [a recent paper in caching systems](https://github.com/Thesys-lab/sosp23-s3fifo). Follow the link mentioned in its [reproducing guide](https://github.com/Thesys-lab/sosp23-s3fifo/blob/6bc49d9630572721b41cd08adfa982775f3cb1de/doc/AE.md) to download the cache traces. To fetch the MSR trace used in the paper:

   ```bash
   mkdir -p data
   wget https://ftp.pdl.cmu.edu/pub/datasets/twemcacheWorkload/cacheDatasets/msr/msr_prxy_1.oracleGeneral.zst -O data/msr.oracleGeneral.zst
   zstd -d data/msr.oracleGeneral.zst -o data/msr.oracleGeneral
   rm data/msr.oracleGeneral.zst
   ```

   The Meta KV trace linked in the paper is no longer hosted. You can recreate an equivalent trace from Meta’s [CacheBench](https://ftp.pdl.cmu.edu/pub/datasets/Baleen24/) dataset on its AWS S3 bucket and convert it with the helper program below. Before running the commands, make sure AWS CLI is installed (e.g., `sudo apt install awscli` on Debian/Ubuntu).

   ```bash
   mkdir -p data
   aws s3 cp --no-sign-request s3://cachelib-workload-sharing/pub/kvcache/202206/kvcache_traces_1.csv data/meta.csv
   g++ -std=c++23 -O3 scripts/convert_meta.cpp -o convert_meta
   ./convert_meta data/meta.csv data/meta.oracleGeneral
   rm convert_meta
   rm data/meta.csv
   ```

2. H&M Personalized Fashion Recommendations from [Kaggle](https://www.kaggle.com/competitions/h-and-m-personalized-fashion-recommendations/data). You should first download `articles.csv.zip` and `transactions_train.csv.zip` from the Kaggle competition page, and then unzip them into `data/articles.csv` and `data/transactions_train.csv` respectively. Then you can use the following command to process the data:

   ```bash
   node scripts/csv.ts select data/articles.csv 0,1 --output data/articles_selected.csv
   rm data/articles.csv
   node scripts/csv.ts select data/transactions_train.csv 0,2 --output data/transactions_train_selected.csv
   rm data/transactions_train.csv
   node scripts/csv.ts join data/transactions_train_selected.csv data/articles_selected.csv 1 0 --output data/hm_joined.csv
   rm data/transactions_train_selected.csv
   rm data/articles_selected.csv
   node scripts/csv.ts select data/hm_joined.csv 0,2 --output data/hm.csv
   rm data/hm_joined.csv
   ```

3. Synthetic e-commerce stream. Use the provided script to generate a synthetic e-commerce stream with [Node.js](https://nodejs.org/) ≥ 24 (run from the project root):

   ```bash
   node scripts/synth.ts --output data/synthetic.csv
   ```

## Benchmark

After building and preparing the datasets, you can run the benchmark program to evaluate the performance of Evolving Sketch.

```bash
$ ./build/benchmark
Usage:
  ./build/benchmark caching [--help] [--version] [--parallel] [--output VAR] trace_path cache_size_ratio adapt_intervals alphas
  ./build/benchmark hm [--help] [--version] [--parallel] [--output VAR] trace_path cache_size_ratio top_k adapt_intervals alphas

$ ./build/benchmark hm
Usage: ./build/benchmark hm [--help] [--version] [--parallel] [--output VAR] trace_path cache_size_ratio top_k adapt_intervals alphas

Positional arguments:
  trace_path        The path to the cache trace file
  cache_size_ratio  The ratio of the cache size to the number of unique objects in the trace
  top_k             The number of top results to return
  adapt_intervals   Comma-separated list of adaptation intervals to use (only used by EvolvingSketch) (e.g., '1000,10000,100000')
  alphas            Comma-separated list of alpha values to use (e.g., '0.1,0.2,0.3')

Optional arguments:
  -h, --help        shows help message and exits
  -v, --version     prints version information and exits
  -p, --parallel    Run all experiments in parallel
  -o, --output      Output file path (as CSV) [nargs=0..1] [default: ""]

$ ./build/benchmark_hm
Usage:
   ./build/benchmark_hm {CMS|ADA|EVO_PRUNING_ONLY|EVO} ...
```

For example, to run Evolving Sketch on the cache trace `data/msr.oracleGeneral` with a cache size ratio of 0.01, an adaptation interval of 10,000, and decay factors 0.5 and 1.0, run:

```bash
./build/benchmark caching data/msr.oracleGeneral 0.01 10000 0.5,1.0
```

Logs print to stdout. To save benchmark results as CSV, pass `--output <file.csv>`.

We also provide a `figures/visualize.ipynb` Jupyter notebook to visualize the benchmark results saved as CSV files. The notebook is written in TypeScript and run in [Jupyter Kernel for Deno](https://docs.deno.com/runtime/reference/cli/jupyter/), employing several libraries such as [Polars](https://www.npmjs.com/package/nodejs-polars) and [Observable Plot](https://observablehq.com/plot/), so you need to install [Deno](https://deno.com/) first and follow the instructions to [install Jupyter Kernel for Deno](https://docs.deno.com/runtime/reference/cli/jupyter/).

## Steps to reproduce

All plots in the paper are generated by the `figures/visualize.ipynb` notebook. To reproduce the results:

Make sure the datasets are prepared as described in “Data Retrieval” before running the commands below.

1. Build the project in Release as described in the “Build” section.
2. Generate the changing DCG curve on the synthetic e-commerce stream for different decay parameters (alpha) to produce the figure used in the introduction:

   ```bash
   mkdir -p output
   ./build/benchmark_hm CMS data/synthetic.csv 4000 1000 10000 0 --trace output/synthetic_cms.trace.csv --progress
   ./build/benchmark_hm EVO_PRUNING_ONLY data/synthetic.csv 4000 1000 10000 0.05 --trace output/synthetic_alpha0.05.trace.csv --progress
   ./build/benchmark_hm EVO_PRUNING_ONLY data/synthetic.csv 4000 1000 10000 500 --trace output/synthetic_alpha500.trace.csv --progress
   ```

3. Run the benchmarks on the cache traces:

   ```bash
   ./build/benchmark caching data/meta.oracleGeneral 0.1 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/meta_large.csv
   ./build/benchmark caching data/meta.oracleGeneral 0.01 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/meta_small.csv
   ./build/benchmark caching data/msr.oracleGeneral 0.1 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/msr_large.csv
   ./build/benchmark caching data/msr.oracleGeneral 0.01 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/msr_small.csv
   ```

4. Run the benchmarks on the online ranking traces (H&M and synthetic e-commerce):

   ```bash
   ./build/benchmark hm data/hm.csv 0.1 1000 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/hm_large.csv
   ./build/benchmark hm data/hm.csv 0.1 100 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/hm_small.csv
   ./build/benchmark hm data/synthetic.csv 0.1 1000 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/synthetic_large.csv
   ./build/benchmark hm data/synthetic.csv 0.1 100 1000,10000,100000 0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,50,100,200,500,1000 -o output/synthetic_small.csv
   ```

5. Record traces to generate the adaptation timeline:

   ```bash
   ./build/benchmark_caching W-TinyLFU_EVO data/meta.oracleGeneral 176705 10000 1 --trace output/meta.trace.csv --progress
   ./build/benchmark_caching W-TinyLFU_EVO data/msr.oracleGeneral 4902 10000 1 --trace output/msr.trace.csv --progress
   ./build/benchmark_hm EVO data/synthetic.csv 4000 100 10000 1 --trace output/synthetic.trace.csv --progress
   ./build/benchmark_hm EVO data/hm.csv 4683 100 10000 1 --trace output/hm.trace.csv --progress
   ```

6. Open `figures/visualize.ipynb` in Jupyter and run all cells to generate the plots.
