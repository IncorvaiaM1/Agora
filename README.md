# Agora

**A multithreaded exchange, agent-based market simulator, CUDA risk engine and OpenGL viewer, written from scratch in C++.**

ECE 4122/6122 Advanced Programming Techniques, Fall 2026, custom final project.

| | |
|---|---|
| Student | Michael Incorvaia (individual project, no team) |
| Special topics | Sockets, `std::thread` (core); CUDA (risk engine); OpenGL (viewer) |
| Status | Week 0: build system, core types, tests. See [PLAN.md](PLAN.md) for the timeline |

---

## What it is

A TCP exchange server runs a limit order book and a price-time-priority matching engine. Separate agent processes connect over sockets and trade against each other as zero-intelligence, trend-following, value and market-maker agents. A CUDA risk engine estimates realized volatility from the live trade tape and prices options and portfolio risk by Monte Carlo. An OpenGL viewer draws order book depth over time.

```
 agents (processes) ──TCP──▶ Session threads ──queue──▶ MatchingEngine (single thread)
                                                             │
                              trade log ◀────────────────────┤
                                                             ▼
          viewer, risk engine, agents ◀──TCP── MarketDataFeed
```

The project is built in three tiers. Tier 1 (exchange + agents) is a complete project on its own; Tiers 2 and 3 add to it without changing it.

| Tier | Component | Topics | Build flag |
|---|---|---|---|
| 1 | Exchange, matching engine, agents, benchmarks | Sockets, `std::thread` | always built |
| 2 | Monte Carlo option pricer, VaR / ES | CUDA | `AGORA_WITH_CUDA` |
| 3 | Depth heatmap viewer | OpenGL | `AGORA_WITH_GL` |

## Custom classes

| Class | Role | Status |
|---|---|---|
| `Order` | One buy/sell instruction with fill state and invariants | done |
| `OrderBook` | Price levels, price-time priority, cancels | planned |
| `MatchingEngine` | Sequences input, matches, emits trades | planned |
| `Session` | One client connection, one gateway thread | planned |
| `SpscRingBuffer` / `MutexQueue` | Gateway to engine queue (compared in results) | planned |
| `Agent` + `ZeroIntelligenceAgent`, `TrendAgent`, `ValueAgent`, `MarketMakerAgent` | Trading strategies | planned |
| `MarketDataFeed` | Broadcasts book updates and trades | planned |
| `PathGenerator`, `OptionPricer` | CUDA Monte Carlo engine with CPU reference | planned |
| `DepthRenderer` | OpenGL heatmap | planned |

## Measurable results

| # | Result |
|---|---|
| 1 | Throughput (orders/s) and p50 / p99 / p99.9 round-trip latency vs number of clients (1, 4, 16, 64) |
| 2 | Mutex queue vs lock-free SPSC ring buffer: throughput and tail latency on the same workload |
| 3 | Market realism: excess kurtosis and autocorrelation of squared returns vs a random-walk baseline |
| 4 | Monte Carlo price vs Black-Scholes, error falling as 1/sqrt(N) for N = 10^4 to 10^8; CUDA vs CPU speedup |
| 5 | Viewer frame rate vs number of book levels drawn |
| 6 | Correctness: book never crosses, volume is conserved, and lockstep or replayed runs give byte-identical trade logs |

Plots and CSVs land in `results/`.

---

## Environment

| | Development | Grading (headless) |
|---|---|---|
| OS | Ubuntu 22.04 / 24.04 (native or WSL2) | PACE-ICE (RHEL) |
| Compiler | GCC 11 or newer, C++17 | GCC module on PACE-ICE |
| Build | CMake 3.16 or newer | CMake module on PACE-ICE |
| CUDA | CUDA Toolkit 12.x (Tier 2 only) | CUDA module on a PACE-ICE GPU node |

Sockets use the POSIX API, so the exchange and agents build on Linux and macOS, not on native Windows. On Windows use WSL2.

### Third-party libraries

| Library | Used for | Link | How it is obtained |
|---|---|---|---|
| doctest 2.4.11 | Unit tests | https://github.com/doctest/doctest | Vendored in `third_party/doctest/` (header only, MIT) |
| GLFW 3 | Viewer window and input | https://www.glfw.org/ | System package (`libglfw3-dev`), viewer only |
| GLEW | OpenGL function loading | https://glew.sourceforge.net/ | System package (`libglew-dev`), viewer only |
| cuRAND | GPU random numbers | https://docs.nvidia.com/cuda/curand/ | Ships with the CUDA Toolkit |

The headless build needs nothing beyond a C++17 compiler and CMake.

---

## Build

```bash
# Full build (exchange, agents, risk engine, viewer)
cmake -S . -B build
cmake --build build -j

# Headless build: no OpenGL. This is the configuration for PACE-ICE.
cmake -S . -B build -DAGORA_WITH_GL=OFF
cmake --build build -j

# CPU only: no CUDA and no OpenGL
cmake -S . -B build -DAGORA_WITH_CUDA=OFF -DAGORA_WITH_GL=OFF
cmake --build build -j
```

If no CUDA compiler is found, CMake prints a warning and builds without the risk engine.

| CMake option | Default | Meaning |
|---|---|---|
| `AGORA_WITH_CUDA` | ON | Build the CUDA risk engine |
| `AGORA_WITH_GL` | ON | Build the OpenGL viewer |
| `AGORA_BUILD_TESTS` | ON | Build the unit tests |
| `AGORA_WERROR` | ON | Treat warnings as errors |

### PACE-ICE

```bash
module load gcc cmake cuda        # exact module versions to be pinned once verified
cmake -S . -B build -DAGORA_WITH_GL=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Test

```bash
ctest --test-dir build --output-on-failure
# or run the test binary directly for doctest's options
./build/agora_tests --success
```

## Run

*Filled in as each component lands.* The intended workflow:

```bash
./build/agora_exchange --port 9000 --seed 42           # start the exchange
./scripts/run_market.sh --agents 16 --seed 42          # launch a mix of agents
./build/agora_viewer --host 127.0.0.1 --port 9001      # optional live viewer
./build/agora_risk --host 127.0.0.1 --port 9001        # optional live risk engine
```

---

## Repository layout

```
include/agora/   public headers, one folder per component
src/core/        Order, OrderBook, MatchingEngine (no I/O, fully unit tested)
src/net/         binary protocol, Session, gateway, market data feed   (planned)
src/queue/       SPSC ring buffer and mutex queue                      (planned)
src/agents/      agent strategies                                      (planned)
src/risk/        CUDA risk engine and CPU reference                    (planned)
src/viewer/      OpenGL depth viewer                                   (planned)
tests/           doctest unit tests
scripts/         run, benchmark, statistics, plotting and packaging scripts
results/         CSVs and plots used in the report
third_party/     vendored dependencies
PLAN.md          plan and timeline
```

## Submission

```bash
./scripts/package.sh        # writes Final_Project.zip from the committed tree
```

## Coding conventions

- Prices are integer ticks, never floating point, so books compare exactly and logs reproduce byte for byte.
- `src/core/` never touches sockets, threads or files; all I/O lives at the edges.
- Every class and public function has a Doxygen comment.
- Zero warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; enforced by `-Werror` and CI.
- Formatting by `clang-format` using `.clang-format` in the repo root.

## License

MIT, see [LICENSE](LICENSE).
