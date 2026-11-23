/**
 * @file risk_main.cpp
 * @brief agora_risk: the Monte Carlo risk engine (Tier 2).
 *
 * Two modes:
 *
 *   agora_risk convergence [--min-paths N] [--max-paths N] [--seeds S]
 *                          [--backend cpu|cuda|both] [--out <file.csv>]
 *
 *     Result 4. Prices one European call and one put by Monte Carlo for path
 *     counts from --min-paths to --max-paths in steps of 10x, with S different
 *     seeds each, on the CPU and (if built with CUDA) on the GPU. Every line
 *     of the CSV compares the estimate with the Black-Scholes value and
 *     records the run time, which gives both the 1/sqrt(N) convergence and
 *     the GPU speedup.
 *
 *   agora_risk live [--host H] [--port P] [--every K] [--paths N] [--seed S]
 *                   [--backend cpu|cuda] [--out <file.csv>] [--agents-out <file.csv>]
 *
 *     Connects to a running exchange as a read-only observer. Every K ticks it
 *     estimates volatility from the prices seen so far, prices an
 *     at-the-money call and put, and computes value at risk and expected
 *     shortfall. When the session ends it writes every agent's position and
 *     risk to --agents-out.
 */
#include "agora/net/client_connection.hpp"
#include "agora/risk/black_scholes.hpp"
#include "agora/risk/option_pricer.hpp"
#include "agora/risk/path_generator.hpp"
#include "agora/risk/risk_monitor.hpp"

#ifdef AGORA_HAS_CUDA
#include "agora/risk/cuda_path_generator.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using namespace agora::risk;

struct Options
{
    std::string mode;
    std::string backend; ///< Empty means the mode's default.
    std::string outPath;
    std::string agentsOutPath;
    std::uint64_t minPaths = 10'000;
    std::uint64_t maxPaths = 100'000'000;
    std::uint64_t seeds = 8;
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    std::uint64_t every = 1000;
    std::uint64_t paths = 200'000;
    std::uint64_t seed = 42;
};

void printUsage()
{
    std::cerr << "usage: agora_risk convergence [--min-paths N] [--max-paths N] [--seeds S]\n"
                 "                              [--backend cpu|cuda|both] [--out <file.csv>]\n"
                 "       agora_risk live [--host H] [--port P] [--every K] [--paths N] [--seed S]\n"
                 "                       [--backend cpu|cuda] [--out <file.csv>] [--agents-out <file.csv>]\n";
}

/// Parses a count that may be written as 100000000 or 1e8.
std::uint64_t parseCount(const std::string& text)
{
    const double value = std::stod(text);
    if (value < 0.0)
    {
        throw std::invalid_argument("negative count");
    }
    return static_cast<std::uint64_t>(std::llround(value));
}

bool parse(int argc, char* argv[], Options& options)
{
    if (argc < 2)
    {
        return false;
    }
    options.mode = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--backend" && hasValue)
        {
            options.backend = argv[++i];
        }
        else if (arg == "--out" && hasValue)
        {
            options.outPath = argv[++i];
        }
        else if (arg == "--agents-out" && hasValue)
        {
            options.agentsOutPath = argv[++i];
        }
        else if (arg == "--min-paths" && hasValue)
        {
            options.minPaths = parseCount(argv[++i]);
        }
        else if (arg == "--max-paths" && hasValue)
        {
            options.maxPaths = parseCount(argv[++i]);
        }
        else if (arg == "--seeds" && hasValue)
        {
            options.seeds = parseCount(argv[++i]);
        }
        else if (arg == "--host" && hasValue)
        {
            options.host = argv[++i];
        }
        else if (arg == "--port" && hasValue)
        {
            options.port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--every" && hasValue)
        {
            options.every = parseCount(argv[++i]);
        }
        else if (arg == "--paths" && hasValue)
        {
            options.paths = parseCount(argv[++i]);
        }
        else if (arg == "--seed" && hasValue)
        {
            options.seed = parseCount(argv[++i]);
        }
        else
        {
            return false;
        }
    }
    return (options.mode == "convergence" || options.mode == "live") && options.minPaths >= 2 &&
           options.maxPaths >= options.minPaths && options.seeds > 0 && options.every > 0 && options.paths >= 2;
}

/// A path generator plus the text that describes it in result files.
struct Backend
{
    std::unique_ptr<PathGenerator> generator;
    std::string device; ///< "single thread" or the GPU's name.
};

Backend makeBackend(const std::string& name)
{
    if (name == "cpu")
    {
        return Backend{std::make_unique<CpuPathGenerator>(), "single thread"};
    }
    if (name == "cuda")
    {
#ifdef AGORA_HAS_CUDA
        auto generator = std::make_unique<CudaPathGenerator>();
        std::string device = generator->deviceName();
        return Backend{std::move(generator), device};
#else
        throw std::runtime_error("this build has no CUDA support (configure with a CUDA compiler available)");
#endif
    }
    throw std::runtime_error("unknown backend: " + name);
}

/// Opens @p path for writing, or returns a closed stream if the path is empty.
std::ofstream openOutput(const std::string& path)
{
    std::ofstream file;
    if (!path.empty())
    {
        file.open(path, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("cannot write " + path);
        }
    }
    return file;
}

// ---------------------------------------------------------------------------
// convergence
// ---------------------------------------------------------------------------

int runConvergence(const Options& options)
{
    // Default: the CPU, plus the GPU if this build has CUDA and a GPU is usable
    // here. "both" insists on the GPU and fails without one.
    std::vector<std::string> names;
    if (options.backend.empty() || options.backend == "both")
    {
        names.push_back("cpu");
#ifdef AGORA_HAS_CUDA
        std::string reason;
        if (CudaPathGenerator::available(reason))
        {
            names.push_back("cuda");
        }
        else if (options.backend == "both")
        {
            throw std::runtime_error("no usable GPU: " + reason);
        }
        else
        {
            std::cout << "no usable GPU (" << reason << "), running the CPU only\n";
        }
#else
        if (options.backend == "both")
        {
            throw std::runtime_error("this build has no CUDA support");
        }
        std::cout << "this build has no CUDA support, running the CPU only\n";
#endif
    }
    else
    {
        names.push_back(options.backend);
    }

    // The textbook test case: slightly out-of-the-money, one year, 20% volatility, 5% rate.
    OptionSpec option;
    option.spot = 100.0;
    option.strike = 105.0;
    option.rate = 0.05;
    option.volatility = 0.2;
    option.maturity = 1.0;

    std::ofstream file = openOutput(options.outPath);
    if (file.is_open())
    {
        file << "backend,device,option,paths,seed,price,std_error,exact,error,z_score,seconds\n";
        file << std::setprecision(12);
    }

    std::uint64_t estimates = 0;
    std::uint64_t beyondThreeSigma = 0;
    double worstZ = 0.0;

    std::cout << std::fixed;
    for (const std::string& name : names)
    {
        Backend backend = makeBackend(name);
        OptionPricer pricer(*backend.generator);
        std::cout << "backend " << name << " (" << backend.device << ")\n";

        // Untimed first call, so one-time start-up costs are not charged to the first measurement.
        pricer.price(option, options.minPaths, 0);

        for (const OptionType type : {OptionType::Call, OptionType::Put})
        {
            option.type = type;
            const char* typeName = type == OptionType::Call ? "call" : "put";
            const double exact = blackScholesPrice(option);

            for (std::uint64_t paths = options.minPaths; paths <= options.maxPaths; paths *= 10)
            {
                double squaredError = 0.0;
                double standardError = 0.0;
                std::vector<double> times;
                for (std::uint64_t seed = 1; seed <= options.seeds; ++seed)
                {
                    const Clock::time_point start = Clock::now();
                    const McEstimate estimate = pricer.price(option, paths, seed);
                    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();

                    const double error = estimate.price - exact;
                    const double z = estimate.standardError > 0.0 ? error / estimate.standardError : 0.0;
                    squaredError += error * error;
                    standardError += estimate.standardError;
                    times.push_back(seconds);
                    ++estimates;
                    beyondThreeSigma += std::fabs(z) > 3.0 ? 1 : 0;
                    worstZ = std::max(worstZ, std::fabs(z));

                    if (file.is_open())
                    {
                        file << name << ',' << backend.device << ',' << typeName << ',' << paths << ',' << seed
                             << ',' << estimate.price << ',' << estimate.standardError << ',' << exact << ','
                             << error << ',' << z << ',' << seconds << '\n';
                    }
                }
                std::sort(times.begin(), times.end());
                const auto count = static_cast<double>(options.seeds);
                std::cout << "  " << std::setw(4) << typeName << "  paths " << std::setw(10) << paths
                          << "  exact " << std::setprecision(5) << exact << "  rms error " << std::setprecision(6)
                          << std::sqrt(squaredError / count) << "  mean std error " << standardError / count
                          << "  median time " << std::setprecision(5) << times[times.size() / 2] << " s\n";
            }
        }
    }

    // About 0.27% of estimates should fall outside 3 standard errors by chance alone.
    std::cout << estimates << " estimates, largest |error| / std error = " << std::setprecision(2) << worstZ << ", "
              << beyondThreeSigma << " beyond 3 (expected by chance: about " << std::setprecision(1)
              << 0.0027 * static_cast<double>(estimates) << ")\n";
    if (file.is_open())
    {
        std::cout << "wrote " << options.outPath << '\n';
    }
    return 0;
}

// ---------------------------------------------------------------------------
// live
// ---------------------------------------------------------------------------

/// One line of the time-series file. Risk columns are for a holder of one share.
void writeReportLine(std::ostream& out, const RiskReport& report)
{
    const TailRisk simulated = positionRisk(1, report.spot, report.simulatedTails);
    const TailRisk exact = positionRisk(1, report.spot, report.analyticTails);
    out << report.tick << ',' << report.spot << ',' << report.volatility << ',' << report.call.price << ','
        << report.call.standardError << ',' << report.callExact << ',' << report.put.price << ','
        << report.put.standardError << ',' << report.putExact << ',' << simulated.valueAtRisk << ','
        << exact.valueAtRisk << ',' << simulated.expectedShortfall << ',' << exact.expectedShortfall << '\n';
}

int runLive(const Options& options)
{
    Backend backend = makeBackend(options.backend.empty() ? "cpu" : options.backend);

    RiskConfig config;
    config.paths = options.paths;
    config.seed = options.seed;
    RiskMonitor monitor(*backend.generator, config);

    std::ofstream file = openOutput(options.outPath);
    if (file.is_open())
    {
        file << "tick,spot,volatility,call_mc,call_std_error,call_exact,put_mc,put_std_error,put_exact,"
                "var_mc,var_exact,es_mc,es_exact\n";
        file << std::setprecision(10);
    }

    // Observer: receives everything, cannot send orders, and the exchange never waits for it.
    agora::net::ClientConnection connection(options.host, options.port);
    agora::net::Hello hello;
    hello.flags = agora::net::kFlagObserver | agora::net::kFlagMarketData;
    connection.send(hello);
    connection.flush();

    std::cout << "agora_risk: " << backend.generator->name() << " (" << backend.device << "), " << options.paths
              << " paths per estimate, report every " << options.every << " ticks\n"
              << std::fixed;

    bool running = true;
    agora::net::ServerMessage message;
    while (running && connection.receive(message))
    {
        std::visit(
            [&](const auto& received)
            {
                using Message = std::decay_t<decltype(received)>;
                if constexpr (std::is_same_v<Message, agora::net::Welcome>)
                {
                    if (received.status != agora::net::WelcomeStatus::Ok)
                    {
                        throw std::runtime_error("exchange refused the connection");
                    }
                }
                else if constexpr (std::is_same_v<Message, agora::net::Tick>)
                {
                    monitor.onTick(received);
                    if (received.tick % options.every == 0)
                    {
                        const RiskReport report = monitor.report();
                        if (file.is_open())
                        {
                            writeReportLine(file, report);
                        }
                        const TailRisk perShare = positionRisk(1, report.spot, report.simulatedTails);
                        std::cout << "tick " << std::setw(7) << report.tick << "  spot " << std::setprecision(1)
                                  << report.spot << "  vol/tick " << std::setprecision(6) << report.volatility
                                  << "  call " << std::setprecision(2) << report.call.price << " (exact "
                                  << report.callExact << ")  VaR/share " << perShare.valueAtRisk << "  ES/share "
                                  << perShare.expectedShortfall << '\n';
                    }
                }
                else if constexpr (std::is_same_v<Message, agora::net::Trade>)
                {
                    monitor.onTrade(received);
                }
                else if constexpr (std::is_same_v<Message, agora::net::End>)
                {
                    running = false;
                }
            },
            message);
    }

    // Final picture: every agent's position and what it could lose over the risk horizon.
    const RiskReport last = monitor.report();
    std::ofstream agentsFile = openOutput(options.agentsOutPath);
    if (agentsFile.is_open())
    {
        agentsFile << "agent,position,var_mc,es_mc,var_exact,es_exact\n" << std::setprecision(10);
    }
    std::cout << "final positions at tick " << last.tick << " (99% value at risk and expected shortfall over "
              << static_cast<std::uint64_t>(config.riskHorizon) << " ticks, in price ticks)\n";
    for (const AgentRisk& agent : last.agents)
    {
        if (agentsFile.is_open())
        {
            agentsFile << agent.agent << ',' << agent.position << ',' << agent.simulated.valueAtRisk << ','
                       << agent.simulated.expectedShortfall << ',' << agent.analytic.valueAtRisk << ','
                       << agent.analytic.expectedShortfall << '\n';
        }
        std::cout << "  agent " << std::setw(3) << agent.agent << "  position " << std::setw(7) << agent.position
                  << "  VaR " << std::setw(12) << std::setprecision(1) << agent.simulated.valueAtRisk << " (exact "
                  << agent.analytic.valueAtRisk << ")  ES " << agent.simulated.expectedShortfall << " (exact "
                  << agent.analytic.expectedShortfall << ")\n";
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    try
    {
        if (!parse(argc, argv, options))
        {
            printUsage();
            return 2;
        }
    }
    catch (const std::exception&)
    {
        printUsage(); // A number failed to parse.
        return 2;
    }

    try
    {
        return options.mode == "convergence" ? runConvergence(options) : runLive(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
