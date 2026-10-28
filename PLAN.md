# Agora: Final Project Plan and Timeline

ECE 6122 custom individual final project. Repo: https://github.com/IncorvaiaM1/Agora

**Hard deadline: Tue Dec 8, 11:59 pm on Canvas. Late projects are not accepted.**
Internal target: submit Mon Dec 7, so a full day of buffer exists.

Source docs in `../`: `Final Project Guidelines.pdf`, `Final_Project_Template_MarketLab.docx` (the statement of work).

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
    net/                    Protocol (binary msgs), Session, Gateway, MarketDataFeed
    queue/                  SpscRingBuffer, MutexQueue (same interface, for result 2)
    agents/                 Agent base, ZeroIntelligence, Trend, Value, MarketMaker, agent_main
    risk/                   PathGenerator, OptionPricer (CUDA .cu + CPU reference), RealizedVol, VaR/ES
    viewer/                 DepthRenderer (GLFW + GLEW), viewer_main
  apps/                     exchange_main, bench_latency, replay_main
  tests/                    unit tests (doctest, header-only, no install needed on PACE)
  scripts/                  run_market.sh, bench.sh, stats.py (kurtosis, ACF), plots.py, package.sh
  results/                  CSVs and PNGs that go into the report and zip
```

Data flow: agents --TCP--> Session threads --queue--> single-threaded MatchingEngine --> trade log + MarketDataFeed --TCP--> agents, viewer, risk engine.

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
| 2 | Nov 9 to Nov 15 | **Lab 5 (sockets) due Thu Nov 12** | Binary protocol, `Session` thread per client, `Gateway`, both queues behind one interface, `MarketDataFeed`. `Agent` base + `ZeroIntelligenceAgent` as separate processes. Seeded fundamental random walk. Lockstep mode. | **M1 (Sun Nov 15):** N ZI agents trade end to end over TCP; lockstep same-seed runs are identical |
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
