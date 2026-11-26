# Agora

**A multithreaded exchange, agent-based market simulator, CUDA risk engine and OpenGL viewer, written from scratch in C++.**

ECE 4122/6122 Advanced Programming Techniques, Fall 2026, custom final project.

The approved statement of work for this project was submitted under the working title **MarketLab**. Agora is the same project; only the name changed.

| | |
|---|---|
| Student | Michael Incorvaia (individual project, no team) |
| Special topics | Sockets, `std::thread` (core); CUDA (risk engine); OpenGL (viewer) |
| Status | Tiers 1 and 2 done with PACE-ICE results (H100 for the GPU). Next: the OpenGL viewer (Tier 3), then the report and video. See [PLAN.md](PLAN.md) |

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
| `PathGenerator` (`CpuPathGenerator`, `CudaPathGenerator`) | Exact geometric Brownian motion samples; one-thread CPU reference and cuRAND GPU version behind one interface | done |
| `OptionPricer` | Monte Carlo European option price with standard error | done |
| `RiskMonitor` | Live market data to realized volatility, option prices, per-agent VaR and expected shortfall | done |
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

### Results

Measured on PACE-ICE node `atl1-1-03-011-3-0` (Intel Xeon Platinum 8462Y+, 12 cores in the session, GCC 12.3.0); details in `results/environment.txt`. Everything below is reproduced by `./scripts/pace_run.sh`.

**Results 1 and 2: throughput and round-trip latency.** Median of 5 repeats, each client keeping one order in flight, 20,000 orders per client.

| Clients | SPSC orders/s | Mutex orders/s | SPSC p50 / p99 / p99.9 (us) | Mutex p50 / p99 / p99.9 (us) |
|---|---|---|---|---|
| 1 | 87,724 | 70,692 | 11.2 / 13.9 / 17.4 | 13.9 / 17.1 / 19.9 |
| 4 | 195,010 | 186,620 | 17.6 / 33.6 / 38.5 | 17.8 / 34.3 / 39.2 |
| 16 | 250,141 | 247,165 | 63.5 / 72.7 / 82.8 | 64.3 / 73.4 / 82.8 |
| 64 | 256,972 | 256,098 | 247.9 / 277.7 / 309.7 | 248.9 / 277.3 / 307.0 |

- The lock-free ring is 24% faster with one client and 4% faster with four. From 16 clients the two are within about 1%: throughput levels off near 250,000 orders/s because the single engine thread is saturated, and its time goes to socket system calls, not to the queue.
- With 16 and 64 clients there are more threads than cores (34 and 130 on 12). There, 2 of 5 SPSC repeats ran 8 to 36% slower, with p99 up to double, while the mutex repeats stayed within 1%. A busy-polling engine thread loses more when the scheduler takes its core away than one that sleeps on a condition variable.
- How the engine waits on the lock-free ring mattered more than the ring itself. An earlier version that called `yield()` while waiting was 20 to 40% slower in about half of all repeats from four clients up; polling with a CPU pause instead removed that at four clients.

Figures: `results/latency_throughput.png`, `results/latency_percentiles.png`.

**Result 3: market realism.** 5 seeds, 50,000 ticks each, lockstep mode, returns over 10-tick bars; mean over seeds, standard deviation in brackets.

| Market | Excess kurtosis | Autocorrelation of squared returns at lags 1, 2, 5, 10 |
|---|---|---|
| Mixed: 2 market makers, 6 zero intelligence, 4 value, 4 trend | 5.87 (3.03) | 0.29, 0.19, 0.07, -0.01 |
| 16 zero-intelligence agents | -0.04 (0.04) | 0.10, 0.02, 0.01, 0.00 |
| Random walk of the same volatility | -0.02 (0.04) | -0.01, 0.00, -0.01, 0.00 |

- The mixed market has fat tails and volatility clustering that lasts several bars; the zero-intelligence market and the random walk have neither.
- The mixed market's price follows the fundamental value; the zero-intelligence market's does not (`results/price_path.png`).
- Two limits of the model: mixed-market returns are positively autocorrelated at lag 1 (0.21, from the trend followers), which real markets are not, and the volatility clustering fades by lag 10, faster than in real data.
- The value agents' position limit was raised from 2,000 to 20,000 shares after a first run in which one seed's price drifted about 560 ticks from the fundamental and then fell 270 ticks in a single step.

Figures: `results/stylized_returns.png`, `results/stylized_acf.png`, `results/price_path.png`.

**Result 4: Monte Carlo risk engine.** A European call and put (spot 100, strike 105, rate 5%, volatility 20%, one year) priced by Monte Carlo for N = 10^4 to 10^8 paths, 8 seeds each, on one CPU thread and on an NVIDIA H100 (PACE-ICE node `atl1-1-03-010-25-0`, CUDA 12.9; `results/environment_gpu.txt`).

- Accuracy: all 160 estimates (2 options x 5 path counts x 8 seeds x 2 backends) agree with Black-Scholes within their own error bars. 116 lie within one standard error and 158 within two (about 109 and 152 expected by chance); the largest deviation is 2.04 standard errors and none exceeds 3.
- Convergence: the RMS error of the call falls from 0.09 at 10^4 paths to 0.001 at 10^8, a factor of about 95 for 10^4 times the paths, against 100 for an exact 1/sqrt(N) law.
- Speed (median time per estimate):

  | Paths | One CPU thread | H100 | Speedup |
  |---|---|---|---|
  | 10^4 | 0.31 ms | 2.39 ms | 0.1x |
  | 10^5 | 3.04 ms | 2.39 ms | 1.3x |
  | 10^6 | 30.4 ms | 2.95 ms | 10x |
  | 10^7 | 304 ms | 2.97 ms | 102x |
  | 10^8 | 3.04 s | 3.06 ms | 994x |

  The CPU time grows exactly linearly with N (about 33 million paths per second). The GPU time is almost flat: up to 10^7 paths it is dominated by a fixed cost per call (allocating and freeing device memory, launching the kernel and copying back one partial sum per thread), not by the simulation. The fastest GPU call at 10^4 paths took 0.38 ms, so most of the 2.4 ms median is that set-up, not something inherent; reusing device buffers between calls would cut it. Below about 10^5 paths the single CPU thread is faster.
- Live: the risk engine follows a 16-agent mixed market as a read-only observer. Every 1,000 ticks it estimates volatility from the prices so far, prices an at-the-money call and put, and computes 99% value at risk and expected shortfall over the next 1,000 ticks for one share and for every agent's position. Over 20,000 ticks on the H100: 38 of the 40 option prices lie within two standard errors of Black-Scholes and the largest deviation is 2.87, as expected for 40 independent estimates; the simulated VaR and expected shortfall of one share stay within 0.6% and 0.9% of the closed form, and every agent's VaR within 0.25%. The run is deterministic like any lockstep run: the positions and volatility path it reports are identical to a CPU run of the same market on another machine.

Figures: `results/risk_convergence.png`, `results/risk_live.png`.

**Result 6: correctness.** The book invariants and volume conservation are checked after every command in the tests and in `agora_replay --verify`. Two lockstep runs with the same seed give byte-identical trade logs, with either queue. The command log of any run, live ones included, replays to the same trade log. The same seeds gave the same statistics, digit for digit, under GCC 11 on Ubuntu and GCC 12 on PACE-ICE.

---

## Environment

| | Development | Grading (headless) |
|---|---|---|
| OS | Ubuntu 22.04 / 24.04 (native or WSL2) | PACE-ICE, RHEL 9 (kernel 5.14) |
| Compiler | GCC 11 or newer, C++17 | GCC 12.3.0 (the PACE-ICE default, no module needed) |
| Build | CMake 3.16 or newer | CMake 3.31.8 (PACE-ICE default) |
| CUDA | CUDA Toolkit 12.x (Tier 2 only) | CUDA 12.9 (`module load cuda`) on a PACE-ICE GPU node; tested on an H100 |

Sockets use the POSIX API, so the exchange and agents build on Linux and macOS, not on native Windows. On Windows use WSL2.

### Third-party libraries

| Library | Used for | Link | How it is obtained |
|---|---|---|---|
| doctest 2.4.11 | Unit tests | https://github.com/doctest/doctest | Vendored in `third_party/doctest/` (header only, MIT) |
| GLFW 3 | Viewer window and input | https://www.glfw.org/ | System package (`libglfw3-dev`), viewer only |
| GLEW | OpenGL function loading | https://glew.sourceforge.net/ | System package (`libglew-dev`), viewer only |
| cuRAND | GPU random numbers | https://docs.nvidia.com/cuda/curand/ | Ships with the CUDA Toolkit |

The headless build needs nothing beyond a C++17 compiler and CMake. It was built and run on PACE-ICE node `atl1-1-03-011-3-0` (Intel Xeon Platinum 8462Y+); `results/environment.txt` records the exact machine, compiler and commit behind the numbers in `results/`.

A lockstep run is reproducible across machines as well as across runs: the same seed gave the same market statistics, digit for digit, under GCC 11 on Ubuntu and GCC 12 on PACE-ICE.

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
| `AGORA_CUDA_ARCHITECTURES` | V100 to H100 | GPU generations to compile for, e.g. `90` for H100 only |

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

### Risk engine (result 4)

```bash
# Convergence and speedup: call and put priced for N = 1e4 ... 1e8 paths, 8 seeds each,
# on the CPU and, in a CUDA build on a GPU node, on the GPU. Every estimate is compared
# with Black-Scholes.
./build/agora_risk convergence --out results/convergence.csv

# Live: a mixed market with the risk engine attached as a read-only observer
./scripts/run_market.sh --mix "mm:2,zi:6,value:4,trend:4" --ticks 20000 --risk cuda   # or --risk cpu

# Everything for Tier 2 on PACE-ICE, from an interactive session on a GPU node
./scripts/pace_run.sh --gpu --quick     # check the setup first
./scripts/pace_run.sh --gpu
```

The live engine estimates volatility from the prices it sees, prices an at-the-money call and put by Monte Carlo next to their Black-Scholes values, and computes every agent's 99% value at risk and expected shortfall over the next 1,000 ticks, next to the closed-form values. Without a GPU, or in a build without CUDA, everything runs on the CPU; the GPU test case is skipped with a message rather than failing.

### Viewer (planned)

```bash
./build/agora_viewer --host 127.0.0.1 --port 9000
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
apps/            command line programs: agora_exchange, agora_agent, agora_risk, agora_bench_latency, agora_replay, agora_engine_bench
src/risk/        risk engine: Black-Scholes, CPU and CUDA path generators, pricer, VaR/ES, live monitor
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
