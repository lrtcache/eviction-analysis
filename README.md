# bufpool-eviction

Small C++ experiments for comparing buffer-pool eviction policies.

The repo currently contains:

- `src/main.cc`: runs the general benchmark suite.
- `src/lruk_experiment.cc`: runs the LRU-K parameter sweep experiments.
- `include/`: eviction policies, trace loaders, benchmark config parsing, and runner code.
- `tests/`: GoogleTest coverage for the runner, buffer pool, and config parser.
- `scripts/Makefile` plus a few C++ helpers for trace conversion and locality analysis.

## Build

Requirements:

- CMake 3.20+
- A C++23 compiler
- Ninja if you want to use the same generator as the top-level `Makefile`

Build the project:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

This produces the main binaries at:

- `build/src/main`
- `build/src/lruk_experiment`

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Run Benchmarks

Both executables take a benchmark config file in INI format:

```bash
build/src/main path/to/benchmarks.ini
build/src/lruk_experiment path/to/benchmarks.ini
```

Each section defines one benchmark. `capacity` is required. The trace source can be synthetic or a binary file.

Minimal example:

```ini
[ZipfRun]
kind = zipf
capacity = 64
length = 100000
page_domain = 1024
alpha = 1.1
seed = 1

[BinaryTraceRun]
kind = binary_file
capacity = 32768
filename = /path/to/trace.bin
```

Common supported `kind` values include:

- `random`
- `range`
- `sampled_range`
- `mixed`
- `hotset_scan`
- `hot_cold_burst`
- `zipf`
- `binary_file`

The programs print a text table of hit/miss statistics to stdout.

## Helper Programs

The tracked helper tools under `scripts/` are plain C++ utilities, built separately from the main CMake project:

```bash
make -C scripts
```

This builds:

- `scripts/convert_msr`
- `scripts/analyze_raw_trace`
- `scripts/analyze_locality`
