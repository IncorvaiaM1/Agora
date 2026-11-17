# Agora

**A multithreaded exchange, agent-based market simulator, CUDA risk engine and OpenGL viewer, written from scratch in C++.**

ECE 4122/6122 Advanced Programming Techniques, Fall 2026, custom final project.

The approved statement of work for this project was submitted under the working title **MarketLab**. Agora is the same project; only the name changed.

| | |
|---|---|
| Student | Michael Incorvaia (individual project, no team) |
| Special topics | Sockets, `std::thread` (core); CUDA (risk engine); OpenGL (viewer) |
| Status | Tier 1 code complete: exchange, four agent strategies, latency benchmark, market statistics and plots. Next: PACE-ICE runs, then the CUDA risk engine. See [PLAN.md](PLAN.md) |

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
| `OrderBook` | Price levels, price-time priority, O(1) cancels, invariant checker | done |
| `MatchingEngine` | Sequences commands, validates, matches, emits events, checks volume conservation | done |
| `EventListener` + `ListenerFanout`, `TradeLogWriter` | Event interface; trade log CSV | done |
| `CommandLogWriter` / `CommandLogReader` | Binary command log for exact replay | done |
| `Rng`, `RandomOrderFlow` | Portable seeded RNG; synthetic load for tests and benchmarks | done |
| `Socket`, `FrameReader` | RAII POSIX TCP socket; reassembles protocol frames from the byte stream | done |
| `Session` | One client connection: identity and batched outbound buffer | done |
| `Gateway` | Acceptor thread plus one reader thread per client | done |
| `SpscRingBuffer` / `MutexQueue` | Lock-free ring and mutex queue (compared in result 2) | done |
| `CommandChannel` (`SpscChannel`, `MutexChannel`) | One interface over both queues, chosen with `--queue` | done |
| `MarketDataFeed` | Broadcasts trades and tick snapshots | done |
| `ExchangeServer` | Engine thread; live and lockstep modes; cancel on disconnect | done |
| `ClientConnection` | Client end of the protocol | done |
| `Agent`, `AgentRunner` | Strategy base class with position, cash and open-order bookkeeping; runner | done |
| `ZeroIntelligenceAgent` | Random orders around the mid; the baseline for result 3 | done |
| `ValueAgent` | Trades toward a noisy estimate of the fundamental value | done |
| `TrendAgent` | Moving-average crossover momentum trader (integer arithmetic) | done |
| `MarketMakerAgent` | Two-sided quotes with inventory skew | done |
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
# The default compiler and CMake on PACE-ICE are new enough for Tier 1; no modules needed.
cmake -S . -B build -DAGORA_WITH_CUDA=OFF -DAGORA_WITH_GL=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure

# Tier 2 only, on a GPU node:
module load cuda
```

`scripts/pace_run.sh` does the build, the tests and every Tier 1 experiment in one command (see Run).

## Test

```bash
ctest --test-dir build --output-on-failure
# or run the test binary directly for doctest's options
./build/agora_tests --success
```

## Run

### Engine tools (available now)

```bash
# Engine microbenchmark: 1M synthetic commands, throughput and per-command latency
./build/agora_engine_bench --commands 1000000 --seed 42

# Record a session's commands, then replay it with full invariant checking
./build/agora_engine_bench --commands 200000 --seed 42 --write-commands session.bin
./build/agora_replay session.bin --trades trades.csv --verify

# Determinism: two replays of the same log give byte-identical trade logs
./build/agora_replay session.bin --trades trades2.csv
cmp trades.csv trades2.csv && echo identical
```

### Exchange and agents (available now)

```bash
# One command: exchange plus 16 zero-intelligence agent processes, lockstep mode, logs in logs/
./scripts/run_market.sh --agents 16 --ticks 5000 --seed 42

# A mixed market: 2 market makers, 6 zero-intelligence, 4 value and 4 trend agents
./scripts/run_market.sh --mix "mm:2,zi:6,value:4,trend:4" --ticks 20000 --seed 42

# Determinism (result 6): the same seed gives a byte-identical trade log,
# whichever queue feeds the engine
./scripts/run_market.sh --agents 16 --ticks 5000 --seed 42 --out logs/a --port 9000
./scripts/run_market.sh --agents 16 --ticks 5000 --seed 42 --out logs/b --port 9001 --queue mutex
cmp logs/a/trades.csv logs/b/trades.csv && echo identical

# Any session, live ones included, replays from its command log
./build/agora_replay logs/a/commands.bin --trades replayed.csv --verify
cmp logs/a/trades.csv replayed.csv && echo identical

# Real-time mode: orders are matched as they arrive, one tick per millisecond
./scripts/run_market.sh --agents 16 --ticks 5000 --live --tick-ms 1

# Or start the pieces by hand
./build/agora_exchange --port 9000 --seed 42 --lockstep --agents 2 --ticks 1000 --trades trades.csv &
./build/agora_agent --type mm --id 1 --seed 42 --port 9000 &
./build/agora_agent --type zi --id 2 --seed 42 --port 9000
```

`agora_agent --type` is one of `zi`, `value`, `trend`, `mm`.

`agora_exchange` options: `--port`, `--bind` (default `127.0.0.1`; use `0.0.0.0` for agents on other nodes), `--queue spsc|mutex`, `--seed`, `--agents N` (wait for N agents before the first tick), `--ticks T`, `--tick-ms M` (live mode; 0 turns ticks off), `--lockstep`, `--trades`, `--commands`, `--tick-log`, `--verify`.

Live mode matches orders in arrival order, which depends on the OS scheduler, so two live runs differ. Lockstep mode buffers each tick's orders until every agent has answered, then matches them in a seeded order, so runs are reproducible.

### Latency and throughput (results 1 and 2)

```bash
# Full sweep: both queues x 1/4/16/64 clients x 5 repeats, written to results/latency.csv
./scripts/bench.sh

# One measurement by hand
./build/agora_exchange --port 9000 --queue spsc --tick-ms 0 &
./build/agora_bench_latency --port 9000 --clients 16 --orders 20000
kill -INT %1
```

Each benchmark client sends limit orders and times the round trip from sending an order to reading its acknowledgement. `--window W` sets how many orders a client keeps in flight: 1 (the default) gives clean latency numbers, larger values give peak throughput. Run the sweep on a quiet machine; on PACE-ICE use a compute node, not a login node.

### Market statistics (result 3)

```bash
# Mixed market and zero-intelligence market, 5 seeds each, then the statistics
./scripts/stylized_facts.sh

# Figures for every result that has data in results/ (needs matplotlib)
python3 scripts/plots.py
```

`stylized_facts.sh` runs each market in lockstep mode with `--tick-log`, then `scripts/stats.py` (standard library only) computes excess kurtosis and the autocorrelation of squared returns against a simulated random walk. Raw logs go to `logs/result3/`; the summary CSVs and figures go to `results/`.

### Everything on PACE-ICE

```bash
# From an interactive session on a compute node, in the repo root
./scripts/pace_run.sh --quick     # about a minute: checks the build and the scripts
./scripts/pace_run.sh             # full run: build, tests, latency sweep, statistics, figures
```

It also writes `results/environment.txt` (node, CPU, compiler and CMake versions, commit).

### Viewer and risk engine (planned)

```bash
./build/agora_viewer --host 127.0.0.1 --port 9000      # optional live viewer
./build/agora_risk --host 127.0.0.1 --port 9000        # optional live risk engine
```

---

## Repository layout

```
include/agora/   public headers, one folder per component
src/core/        Order, OrderBook, MatchingEngine, RandomOrderFlow (no I/O, fully unit tested)
src/io/          command log (binary, for replay) and trade log (CSV)
include/agora/queue/  SPSC ring buffer and mutex queue (header-only templates)
src/net/         binary protocol, sockets, Session, Gateway, market data feed, exchange server
src/agents/      Agent base class, runner and strategies
apps/            command line programs: agora_exchange, agora_agent, agora_bench_latency, agora_replay, agora_engine_bench
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
- All randomness goes through `agora::Rng` (xoshiro256**), never `std::uniform_*_distribution`, whose output differs between standard libraries. Same seed gives the same trade log under GCC and Clang.
- `src/core/` never touches sockets, threads or files; all I/O lives at the edges.
- Every class and public function has a Doxygen comment.
- Zero warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; enforced by `-Werror` and CI.
- Formatting by `clang-format` using `.clang-format` in the repo root.

## License

MIT, see [LICENSE](LICENSE).
