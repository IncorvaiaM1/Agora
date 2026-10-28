/**
 * @file engine_bench.cpp
 * @brief agora_engine_bench: single-threaded matching engine microbenchmark.
 *
 * Measures the engine alone, with no sockets or queues, as the baseline for
 * the end-to-end latency results.
 *
 * Usage:
 *   agora_engine_bench [--commands N] [--seed S] [--repeats R]
 *                      [--write-commands <file.bin>] [--csv]
 *
 * Steps:
 *   1. Generate N commands with RandomOrderFlow driving a scratch engine
 *      (the flow needs the engine's order ids to issue cancels).
 *   2. Throughput: replay the recorded commands into a fresh engine R times,
 *      timing each whole run.
 *   3. Latency: replay once more, timing every process() call, and report
 *      p50 / p99 / p99.9 / max.
 */
#include "agora/core/matching_engine.hpp"
#include "agora/core/random_order_flow.hpp"
#include "agora/io/command_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

struct Options
{
    std::size_t commands = 1'000'000;
    std::uint64_t seed = 42;
    int repeats = 5;
    std::string writeCommandsPath;
    bool csv = false;
};

bool parse(int argc, char* argv[], Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--commands" && hasValue)
        {
            options.commands = std::stoull(argv[++i]);
        }
        else if (arg == "--seed" && hasValue)
        {
            options.seed = std::stoull(argv[++i]);
        }
        else if (arg == "--repeats" && hasValue)
        {
            options.repeats = std::stoi(argv[++i]);
        }
        else if (arg == "--write-commands" && hasValue)
        {
            options.writeCommandsPath = argv[++i];
        }
        else if (arg == "--csv")
        {
            options.csv = true;
        }
        else
        {
            return false;
        }
    }
    return options.commands > 0 && options.repeats > 0;
}

/// Value at quantile q (0..1) of an already sorted sample.
std::int64_t quantile(const std::vector<std::int64_t>& sorted, double q)
{
    const auto index = static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    if (!parse(argc, argv, options))
    {
        std::cerr << "usage: agora_engine_bench [--commands N] [--seed S] [--repeats R]"
                     " [--write-commands file.bin] [--csv]\n";
        return 2;
    }

    try
    {
        // 1. Generate the workload.
        agora::FlowConfig flowConfig;
        flowConfig.seed = options.seed;
        agora::RandomOrderFlow flow(flowConfig);
        agora::MatchingEngine generator(flow);
        std::vector<agora::Command> commands;
        commands.reserve(options.commands);
        for (std::size_t i = 0; i < options.commands; ++i)
        {
            commands.push_back(flow.next());
            generator.process(commands.back());
        }
        generator.verify();

        if (!options.writeCommandsPath.empty())
        {
            std::ofstream out(options.writeCommandsPath, std::ios::binary);
            agora::CommandLogWriter writer(out);
            for (const agora::Command& command : commands)
            {
                writer.write(command);
            }
        }

        // 2. Throughput.
        double bestSeconds = 1e300;
        for (int r = 0; r < options.repeats; ++r)
        {
            agora::NullListener sink;
            agora::MatchingEngine engine(sink);
            const auto start = Clock::now();
            for (const agora::Command& command : commands)
            {
                engine.process(command);
            }
            const std::chrono::duration<double> elapsed = Clock::now() - start;
            bestSeconds = std::min(bestSeconds, elapsed.count());
        }
        const double commandsPerSecond = static_cast<double>(commands.size()) / bestSeconds;

        // 3. Per-command latency.
        std::vector<std::int64_t> latencies;
        latencies.reserve(commands.size());
        {
            agora::NullListener sink;
            agora::MatchingEngine engine(sink);
            for (const agora::Command& command : commands)
            {
                const auto start = Clock::now();
                engine.process(command);
                const auto stop = Clock::now();
                latencies.push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
            }
        }
        std::sort(latencies.begin(), latencies.end());

        const agora::EngineStats& stats = generator.stats();
        if (options.csv)
        {
            std::cout << "commands,seed,trades,best_seconds,commands_per_sec,p50_ns,p99_ns,p999_ns,max_ns\n"
                      << commands.size() << ',' << options.seed << ',' << stats.trades << ',' << bestSeconds
                      << ',' << commandsPerSecond << ',' << quantile(latencies, 0.50) << ','
                      << quantile(latencies, 0.99) << ',' << quantile(latencies, 0.999) << ','
                      << latencies.back() << '\n';
        }
        else
        {
            std::cout << std::fixed << std::setprecision(2) << "workload   : " << commands.size()
                      << " commands, seed " << options.seed << ", " << stats.trades << " trades, "
                      << stats.rejected << " rejects, " << generator.book().orderCount()
                      << " resting at end\n"
                      << "throughput : " << commandsPerSecond / 1e6 << " M commands/s (best of "
                      << options.repeats << ")\n"
                      << "latency ns : p50 " << quantile(latencies, 0.50) << ", p99 "
                      << quantile(latencies, 0.99) << ", p99.9 " << quantile(latencies, 0.999) << ", max "
                      << latencies.back() << "  (includes clock overhead)\n";
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
