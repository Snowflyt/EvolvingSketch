# Evolving Sketch

This repository hosts the implementation and benchmarking of Evolving Sketch, an adaptively parameterized time-decaying frequency sketch for online streams.

## Software Architecture

- Programming Language: C++
- Build Tool: CMake
- Logging: spdlog
- Testing: Catch2
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

Several real-world datasets are used in the benchmarking of Evolving Sketch. Follow the instructions below to retrieve and prepare the datasets.

Preprocessing scripts are implemented in TypeScript and are located in the `scripts/` directory, and you can run them with [Node.js](https://nodejs.org) ≥ 23. The following code snippets assume you **run commands in the project root directory**.

1. Cache trace. The cache traces we used are from [a recent paper in caching systems](https://github.com/Thesys-lab/sosp23-s3fifo). Follow the link mentioned in its [reproducing guide](https://github.com/Thesys-lab/sosp23-s3fifo/blob/6bc49d9630572721b41cd08adfa982775f3cb1de/doc/AE.md) to download the cache traces. Specially, we use the following 2 traces:

   ```shell
   mkdir -p data
   wget https://ftp.pdl.cmu.edu/pub/datasets/twemcacheWorkload/cacheDatasets/metaKV/meta_kvcache_traces_1.oracleGeneral.bin.zst -O data/meta.oracleGeneral.zst
   zstd -d data/meta.oracleGeneral.zst -o data/meta.oracleGeneral  # decompress the file
   rm data/meta.oracleGeneral.zst  # remove the compressed file
   wget https://ftp.pdl.cmu.edu/pub/datasets/twemcacheWorkload/cacheDatasets/msr/msr_prxy_1.oracleGeneral.zst -O data/msr.oracleGeneral.zst
   zstd -d data/msr.oracleGeneral.zst -o data/msr.oracleGeneral  # decompress the file
   rm data/msr.oracleGeneral.zst  # remove the compressed file
   ```

2. H&M Personalized Fashion Recommendations from [Kaggle](https://www.kaggle.com/competitions/h-and-m-personalized-fashion-recommendations/data). You should first download `articles.csv.zip` and `transactions_train.csv.zip` from the Kaggle competition page, and then unzip them into `data/articles.csv` and `data/transactions_train.csv` respectively. Then you can use the following command to process the data:

   ```shell
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

## Benchmark

After building and preparing the datasets, you can run the benchmark program to evaluate the performance of Evolving Sketch.

```bash
$ ./build/benchmark
Usage:
  ./build/benchmark caching <trace_path> <cache_size_ratio> <α1,α2,...>
  ./build/benchmark hm <trace_path> <cache_size_ratio> <top_k> <α1,α2,...>
```

For example, to benchmark Evolving Sketch on the cache trace `data/msr.oracleGeneral` with a cache size ratio of 0.1 and decay factors of 0.5 and 1.0, you can run:

```bash
./build/benchmark caching data/msr.oracleGeneral 0.01 0.5,1.0
```

Logs will be written to standard output, and you can redirect them to a file if needed. After the benchmark is finished, results will be printed as CSV format, which you can copy and paste into a CSV file for further analysis.

We also provide a `figures/visualize.ipynb` Jupyter notebook to visualize the benchmark results saved as CSV files. The notebook is written in TypeScript and run in [Jupyter Kernel for Deno](https://docs.deno.com/runtime/reference/cli/jupyter/), employing several libraries such as [Polars](https://www.npmjs.com/package/nodejs-polars) and [Observable Plot](https://observablehq.com/plot/), so you need to install [Deno](https://deno.com/) first and follow the instructions to [install Jupyter Kernel for Deno](https://docs.deno.com/runtime/reference/cli/jupyter/).
