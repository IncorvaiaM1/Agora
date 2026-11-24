# Agora: Final Project Plan and Timeline

ECE 6122 custom individual final project. Repo: https://github.com/IncorvaiaM1/Agora

**Hard deadline: Tue Dec 8, 11:59 pm on Canvas. Late projects are not accepted.**
Internal target: submit Mon Dec 7, so a full day of buffer exists.

Source docs in `../`: `Final Project Guidelines.pdf`, `Final_Project_Template_MarketLab.docx` (the statement of work).

---

## 0. Status (updated Mon Nov 16)

**M1 is met, one day after its Nov 15 date.** Nothing landed between Oct 28 and Nov 15, then the whole networking layer went in on Nov 16, so Tier 1 is back on the original schedule rather than a week behind.

Done:
- Core (Oct 27 to 28): order book, matching engine, invariant and conservation checks, command log, `agora_replay`, engine benchmark.
- Networking (Nov 16): binary wire protocol, POSIX `Socket`, `Session`, `Gateway` (one reader thread per client), `SpscRingBuffer` and `MutexQueue` behind one `CommandChannel` interface (`--queue spsc|mutex`), `MarketDataFeed`, `ExchangeServer` with live and lockstep modes, cancel on disconnect.
- Agents (Nov 16): `Agent` base class, `AgentRunner`, `ZeroIntelligenceAgent`, `agora_agent`, `scripts/run_market.sh`.
- Strategies and benchmark (Nov 16): `ValueAgent`, `TrendAgent`, `MarketMakerAgent`, `--mix` in `run_market.sh`, `agora_bench_latency`, `scripts/bench.sh`.
- 55 test cases pass (GCC 11, also under ASan + UBSan). TSan is clean on the SPSC path; on the mutex path GCC 11's libtsan reports false positives from `condition_variable::wait_for` (it does not intercept `pthread_cond_clockwait`), which disappear when the wait uses the system clock.

Measured on Nov 16 (16 ZI agent processes, 5000 ticks, lockstep, seed 42): 48,302 commands and 14,975 trades in about 2 s; two runs (one per queue kind) give byte-identical trade logs; `agora_replay` of the command log reproduces the trade log, in live mode too.

First look at result 3 (20,000 ticks, lockstep, seed 42, returns over 20-trade bars, quick throwaway script): the mixed market `mm:2,zi:6,value:4,trend:4` has excess kurtosis 2.5 and squared-return autocorrelation 0.16, 0.16, 0.10 at lags 1, 2, 5; 16 ZI agents alone give 1.1 and 0.15, 0.02, -0.01. So the mix shows fatter tails and volatility clustering that the baseline lacks. In the mix, market makers and value agents end with a profit and ZI and trend agents with a loss. These are one seed; `stats.py` should average over several.

Scripts (Nov 17): exchange `--tick-log`, `scripts/stats.py`, `scripts/stylized_facts.sh`, `scripts/plots.py`, `scripts/pace_run.sh`. Trial run off PACE (3 seeds, 30,000 ticks, 10-tick bars): mixed market excess kurtosis 4.1 (std 1.4 over seeds), squared-return autocorrelation 0.24, 0.19, 0.08 at lags 1, 2, 5; ZI-only and random walk both about 0 kurtosis. The mixed market tracks the fundamental; the ZI market does not. One thing to state honestly in the report: mixed-market returns themselves are positively autocorrelated at lag 1 (about 0.22, from the trend followers), which real markets are not.

First PACE-ICE run (Nov 17, commit 428d44a, node atl1-1-03-011-3-0, Xeon Platinum 8462Y+, 12 cores, GCC 12.3.0). All tests passed there.

- Results 1 and 2 (medians of 5 repeats, one order in flight per client):

  | Clients | SPSC orders/s | Mutex orders/s | SPSC p50 / p99 / p99.9 (us) | Mutex p50 / p99 / p99.9 (us) |
  |---|---|---|---|---|
  | 1 | 78,858 | 70,386 | 12.3 / 15.3 / 18.7 | 14.0 / 17.3 / 20.4 |
  | 4 | 173,229 | 187,881 | 18.9 / 37.5 / 42.1 | 17.7 / 34.1 / 39.0 |
  | 16 | 242,916 | 247,942 | 65.4 / 75.1 / 86.3 | 64.1 / 73.4 / 84.1 |
  | 64 | 254,861 | 256,951 | 250.0 / 280.2 / 308.7 | 248.1 / 277.5 / 308.2 |

  The lock-free queue wins by about 12% with one client and is level with or slightly behind the mutex queue from 4 clients up. Throughput flattens near 250,000 orders/s from 16 clients: the single engine thread is the limit, and its time goes to socket system calls, not to the queue. That is the finding for result 2, and it is a legitimate one.
- One cause of the SPSC queue trailing at 4 clients was mine: the engine polled all 128 session rings on every pop, used or not. Fixed after this run (it now polls only slots that have had a client), so the next run may move the SPSC numbers.
- Result 3: seeds 1, 2, 3 and 5 gave excess kurtosis 2.4 to 6.1, but seed 4 gave 953. Cause: the fundamental fell about 600 ticks, the four value agents hit their 2,000-share position limit, the price stayed about 560 ticks too high, and then fell 270 ticks in one step when the bids ran out. Fixed after this run by raising the value agents' limit to 20,000 shares. With that, a local rerun of the five seeds gives excess kurtosis 5.9 (std 3.0, range 2.8 to 11.5) and squared-return autocorrelation 0.29, 0.19, 0.07 at lags 1, 2, 5.
- The same seed produced identical statistics on PACE-ICE (GCC 12) and on Ubuntu (GCC 11).

Second PACE-ICE run (Nov 17, commit 4befb88, same node):

- Result 3 is settled. Mixed market: excess kurtosis 5.9 (std 3.0 over 5 seeds, range 2.8 to 11.5); squared-return autocorrelation 0.29, 0.19, 0.07, -0.01 at lags 1, 2, 5, 10. ZI only: kurtosis 0.0; 0.10, 0.02, 0.01, 0.00. Random walk: kurtosis 0.0; all lags about 0. The PACE-ICE numbers match the local rerun to every printed digit.
- Results 1 and 2 are not settled. SPSC improved with one client (90,781 orders/s, p50 10.8 us, against mutex 70,622 and 13.9 us) but became erratic from 4 clients up: individual repeats were either level with the mutex queue (191k at 4 clients, 249k at 16, 255k at 64) or 20 to 40 percent slower with p99 about doubled. The mutex queue was steady apart from three slow repeats.
- Working explanation, not yet confirmed: while waiting for the next order the SPSC engine thread called `yield()`, which on a busy node gives its core away, and it is the one thread everything waits for. Changed after this run to busy-poll with a CPU pause first, and to yield and then sleep only after longer idle periods. Run 3 tests this.

Third PACE-ICE run (Nov 17, same node, with the busy-poll change): the explanation held where cores are not oversubscribed. SPSC is now steady and ahead at 1 client (87,724 against 70,692 orders/s) and at 4 (195,010 against 186,620). At 16 and 64 clients the medians are level (about 250k and 257k), but 2 of 5 SPSC repeats were still 8 to 36% slower, with 34 and 130 threads on 12 cores. No more tuning: this is the result, and it is written up in the README under "Results so far".

**Tier 1 is complete (Nov 17), five days ahead of M2.** Results 1, 2, 3 and 6 have CSVs, figures and a README write-up.

Tier 2 code (Nov 17): `PathGenerator` with `CpuPathGenerator` (one thread, the reference) and `CudaPathGenerator` (cuRAND Philox, one stream per GPU thread, each thread sums its own payoffs so memory does not grow with N); `OptionPricer`; Black-Scholes; realized volatility; simulated and closed-form VaR/ES; `RiskMonitor`; `agora_risk convergence` and `agora_risk live`; `--risk` in `run_market.sh`; `scripts/risk_run.sh`; `pace_run.sh --gpu`; two new figures. Paths are sampled exactly from the lognormal law (no time stepping), so the only error is Monte Carlo noise.

Checked so far, without a GPU: 64 test cases pass (GCC 11). CPU convergence to 1e7 paths: every estimate within 1.96 standard errors of Black-Scholes; error falls as 1/sqrt(N). Live run on a 16-agent mixed market: option prices within error bars of Black-Scholes at every report, simulated VaR/ES within about 1% of the closed form. The CUDA file compiles cleanly for sm_90 (CUDA 13.5, -Wall -Wextra; 56 registers, no spills), and the whole CMake build with CUDA enabled builds and links under GCC 13 with warnings as errors. The CUDA runtime is linked statically, so programs start without `module load cuda`. Not yet run on a GPU.

GPU run on PACE-ICE (Nov 23, H100 80GB, CUDA 12.9, node atl1-1-03-010-25-0): the CUDA build and all 64 tests passed, including the GPU generator. Convergence: all 160 estimates within 2.04 standard errors of Black-Scholes. Speedup over one CPU thread: 0.1x at 1e4 paths, 1.3x at 1e5, 10x at 1e6, 102x at 1e7, 994x at 1e8. GPU time is nearly flat (2.4 to 3.1 ms) because a fixed per-call cost dominates; noted in the README rather than tuned. Written up in the README under Results.

One bug found from that run: the live risk engine set up the GPU before connecting, so on the H100 it joined the market late, missed the opening trades and reported positions offset by a constant (for example 243 instead of 203 shares). Fixed: `agora_risk` prints "connected" after the handshake and `run_market.sh` starts the agents only after that line. Verified with an artificial 2-second start-up delay. The live files from that run (`results/risk_live.*`, `results/risk_agents.csv`) must be regenerated.

**Tier 2 is complete (Nov 23), six days ahead of M3,** apart from regenerating the live risk files.

Left:
1. On a PACE-ICE GPU node: `module load cuda && cmake --build build-cuda -j && ./scripts/risk_run.sh --skip-convergence && python3 scripts/plots.py`, then commit `results/`.
2. Tier 3, the OpenGL viewer: decide on Wed Dec 2 whether to keep or cut it. Then the LaTeX report and the video; code freeze Sat Dec 5.

Still open from section 2: confirm what the submitted SOW says (name, result 6 wording). Result 6 as originally worded ("two runs with the same seed produce byte-identical trade logs") now holds in lockstep mode, which is the default of `run_market.sh`.

---

## 1. What has to be true at submission (from the guidelines)

| Requirement | How Agora meets it |
|---|---|
| Verifiable: video, or compiles on PACE-ICE | Both. Video of full system; headless core (exchange, agents, risk) builds on PACE-ICE with no OpenGL |
| README: OS, compiler, third-party libs with links, build steps | `README.md` at repo root, written last but stubbed in week 0 |
| Custom classes | Order, OrderBook, MatchingEngine, Session, SpscRingBuffer, Agent hierarchy, MarketDataFeed, PathGenerator, OptionPricer, DepthRenderer |
| At least one special topic | Sockets + std::thread (core). CUDA and OpenGL are extra tiers |
| Measurable results | Six results in the SOW (section 4 below) |
| Coding style, no warnings | `-Wall -Wextra -Wpedantic -Werror` from day one, clang-format, Doxygen-style comments |
| Single `Final_Project.zip` | Packaging script, tested by unzipping and building from scratch on PACE-ICE |

Grading risk is front-loaded: "functions as expected" is judged against the SOW, so Tier 1 must be solid before anything else gets attention.

---

## 2. Open items to settle this week

1. **Rename the SOW.** The docx says "MarketLab"; the repo is Agora. Update the project name before submitting so the TAs grade the thing they see.
2. **Submit the SOW ASAP** (target Thu Oct 29). TAs can request changes or reject; finding out in week 1 is cheap, finding out in week 4 is not.
3. **Fix determinism claim (result 6).** With many socket clients, arrival order at the gateway is set by the OS scheduler, so two live runs with the same seed will *not* give byte-identical trade logs. Two ways to keep the claim honest:
   - **Lockstep mode:** the exchange runs on a simulation clock; each tick it collects one message (or a no-op) from every agent, sorts by agent id, then matches. Deterministic, still uses sockets and threads.
   - **Replay mode:** the engine logs its sequenced input stream; replaying that file through the engine reproduces the trade log byte for byte.
   Plan: implement both (replay is ~50 lines once the input log exists), and reword result 6 in the SOW to "same seed in lockstep mode, or replay of a recorded session, yields byte-identical trade logs".
4. **PACE-ICE access check:** confirm a CUDA GPU node and a recent g++/CMake module are available; note the module names for the README.

---

## 3. Architecture and repo layout

```
Agora/
  CMakeLists.txt            options: AGORA_WITH_CUDA, AGORA_WITH_GL (both default ON, OFF for headless)
  README.md
  include/agora/            public headers
  src/
    core/                   Order, OrderBook, MatchingEngine (no I/O, fully unit-testable)
    net/                    protocol, Socket, Session, Gateway, CommandChannel, MarketDataFeed, ExchangeServer, ClientConnection
    (include/agora/queue/)  SpscRingBuffer, MutexQueue (header-only templates, for result 2)
    agents/                 Agent base, ZeroIntelligence, Trend, Value, MarketMaker, agent_main
    risk/                   PathGenerator, OptionPricer (CUDA .cu + CPU reference), RealizedVol, VaR/ES
    viewer/                 DepthRenderer (GLFW + GLEW), viewer_main
  apps/                     exchange_main, bench_latency, replay_main
  tests/                    unit tests (doctest, header-only, no install needed on PACE)
  scripts/                  run_market.sh, bench.sh, stats.py (kurtosis, ACF), plots.py, package.sh
  results/                  CSVs and PNGs that go into the report and zip
```

Data flow: agents --TCP--> Session threads --queue--> single-threaded MatchingEngine --> trade log + MarketDataFeed --TCP--> agents, viewer, risk engine.

As built: one port. The viewer and risk engine connect like agents but send `Hello` with the observer and market-data flags, so they receive every trade and tick, cannot send orders, and are not waited for in lockstep mode. The fundamental value is generated by the exchange and broadcast in each `Tick`.

Design rule: `core/` never touches sockets or threads. That keeps the engine testable and makes replay mode trivial.

---

## 4. Measurable results (the grading contract)

| # | Result | Produced by | Done in |
|---|---|---|---|
| 1 | Throughput (orders/s) and p50/p99/p99.9 RTT vs clients (1, 4, 16, 64) | `bench_latency`, `bench.sh` | Wk 3 |
| 2 | Mutex queue vs SPSC ring buffer: throughput and tail latency, same seeds | same harness, flag switch | Wk 3 |
| 3 | Excess kurtosis and ACF of squared returns vs random-walk baseline | `stats.py` on trade tape | Wk 3 |
| 4 | MC price vs Black-Scholes, error ~ 1/sqrt(N) for N = 1e4..1e8; GPU vs 1-thread CPU speedup | `risk` bench | Wk 4 |
| 5 | Viewer FPS vs book levels drawn; video of full system | viewer + recording | Wk 5 |
| 6 | Book never crosses; traded volume conserved; deterministic trade log (see 2.3) | unit tests + invariant checks in engine | Wk 1 to 2 |

---

## 5. Timeline

Today is Tue Oct 27. Six weeks to Dec 8. Other ECE 6122 load is marked so it is planned around, not discovered.

| Week | Dates | Other 6122 load | Agora work | Exit criteria |
|---|---|---|---|---|
| 0 | Oct 27 to Nov 1 | | Rename + submit SOW (Thu Oct 29). Repo skeleton: CMake with options, warnings-as-errors, clang-format, doctest, GitHub Actions build (done Oct 27). Pulled forward: `OrderBook` + `MatchingEngine`, invariant checks, command log + replay, engine microbenchmark. | SOW submitted; core engine and replay working with tests |
| 1 | Nov 2 to Nov 8 | **Lab 4 (CUDA) due Sun Nov 8**; TCP sockets lectures | Lab 4. Harden the core: engine benchmark numbers on PACE-ICE, profile and tune the book if needed. Start the binary wire protocol. | Core results for result 6 recorded; Lab 4 submitted |
| 2 | Nov 9 to Nov 15 | **Lab 5 (sockets) due Thu Nov 12** | Binary protocol, `Session` thread per client, `Gateway`, both queues behind one interface, `MarketDataFeed`. `Agent` base + `ZeroIntelligenceAgent` as separate processes. Seeded fundamental random walk. Lockstep mode. | **M1 (Sun Nov 15):** N ZI agents trade end to end over TCP; lockstep same-seed runs are identical. **Met Mon Nov 16.** |
| 3 | Nov 16 to Nov 22 | Design patterns lectures | Trend, Value, MarketMaker agents. Latency harness and sweeps (results 1, 2). `stats.py` stylized facts (result 3). First full run on PACE-ICE headless. | **M2 (Sun Nov 22): Tier 1 done.** Results 1, 2, 3, 6 have CSVs and plots. This is a complete, gradable project on its own |
| 4 | Nov 23 to Nov 29 | Thanksgiving Nov 25 to 27 | CUDA risk engine: CPU reference first, then cuRAND GBM paths, European call/put, convergence sweep 1e4 to 1e8, speedup, realized vol from tape, VaR/ES on agent positions. Book PACE GPU time early in the week. | **M3 (Sun Nov 29): Tier 2 done.** Result 4 plots; MC within 3 sigma of BS at every N |
| 5 | Nov 30 to Dec 6 | **Lab 6 (MPI) due Thu Dec 3** | OpenGL viewer: depth heatmap texture scrolling over time, mid-price line, risk overlay text. FPS vs levels (result 5). **Cut decision Wed Dec 2:** if the heatmap is not drawing live data by then, drop Tier 3 and spend the time on polish. **Code freeze Sat Dec 5.** | Viewer works (or is cut cleanly); all code commented, zero warnings |
| 6 | Dec 6 to Dec 8 | Last day of class Dec 8 | Record video (full system + results walkthrough). Finish README and report (LaTeX). `package.sh` -> `Final_Project.zip`; unzip on PACE-ICE and build headless from scratch. | **Submit Mon Dec 7.** Dec 8 is buffer only |

### Slack and cut order
If behind, cut in this order, and update the report wording to match what was delivered:
1. Viewer risk overlay (keep heatmap + mid line)
2. VaR/ES on positions (keep option pricer + convergence + speedup)
3. 64-client point in the latency sweep (keep 1, 4, 16)
4. Whole OpenGL tier
5. Whole CUDA tier (Tier 1 alone still satisfies every guideline)

Never cut: unit tests, invariant checks, README, the video.

---

## 6. Risks

| Risk | Mitigation |
|---|---|
| SOW rejected or changes requested | Submit by Oct 29; tiered scope means any requested trim is easy |
| Nondeterminism breaks result 6 | Lockstep + replay modes (section 2.3) |
| PACE GPU queue waits | Book GPU sessions early in week 4; develop the CUDA kernels locally if a local GPU is available |
| Windows vs Linux sockets | Develop and benchmark on Linux (WSL/PACE); POSIX sockets only, documented in README |
| Latency numbers dominated by loopback/OS noise | Pin threads where possible, report medians over 5 runs, state the machine in the report |
| OpenGL not available on PACE | Expected; headless build flag, viewer shown in video |
| Third-party libs | Only GLFW + GLEW (viewer), doctest (header-only, vendored). All linked in README |

---

## 7. Final checklist (week 6)

- [ ] `Final_Project.zip` contains all source, scripts, results, README, report PDF
- [ ] README: OS, compiler + version, CMake version, CUDA version, GLFW/GLEW links, exact build and run commands, PACE module lines
- [ ] Clean build with zero warnings in both full and headless configs
- [ ] Every result in section 4 has a plot or table in the report
- [ ] Video shows: exchange start, agents connecting, live viewer, risk numbers, final plots
- [ ] Headless build verified on PACE-ICE from the unzipped archive
- [ ] Submitted on Canvas by Dec 7
