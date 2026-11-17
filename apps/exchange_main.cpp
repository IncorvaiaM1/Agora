/**
 * @file exchange_main.cpp
 * @brief agora_exchange: the exchange server.
 *
 * Usage:
 *   agora_exchange [--port P] [--bind ADDR] [--queue spsc|mutex] [--seed S]
 *                  [--agents N] [--ticks T] [--tick-ms M] [--lockstep]
 *                  [--trades <out.csv>] [--commands <out.bin>] [--tick-log <out.csv>]
 *                  [--verify]
 *
 * Live mode (default): matches orders as they arrive and publishes a Tick
 * every --tick-ms milliseconds (0 turns ticks off). Runs for --ticks ticks,
 * or until Ctrl-C if --ticks is not given.
 *
 * Lockstep mode (--lockstep, needs --agents and --ticks): waits for N agents,
 * then advances a simulation clock one tick at a time. The same seed and the
 * same agents give a byte-identical trade log on every run.
 *
 * --commands records the sequenced command stream; agora_replay turns it
 * back into the same trade log. --tick-log records the fundamental value and
 * the best bid and ask at every tick, for the market statistics.
 */
#include "agora/net/exchange_server.hpp"

#include <atomic>
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

/// Set by SIGINT / SIGTERM; the main thread forwards it to the server.
std::atomic<agora::net::ExchangeServer*> g_server{nullptr};

extern "C" void onSignal(int /*signal*/)
{
    // Only an atomic load and an atomic store: safe inside a signal handler.
    if (agora::net::ExchangeServer* server = g_server.load())
    {
        server->requestStop();
    }
}

struct Options
{
    agora::net::ExchangeConfig exchange;
    std::string tradesPath;
    std::string commandsPath;
    std::string tickLogPath;
};

void printUsage()
{
    std::cerr << "usage: agora_exchange [--port P] [--bind ADDR] [--queue spsc|mutex] [--seed S]\n"
                 "                      [--agents N] [--ticks T] [--tick-ms M] [--lockstep]\n"
                 "                      [--trades <out.csv>] [--commands <out.bin>] [--tick-log <out.csv>]\n"
                 "                      [--verify]\n";
}

bool parse(int argc, char* argv[], Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--port" && hasValue)
        {
            options.exchange.gateway.port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--bind" && hasValue)
        {
            options.exchange.gateway.bindHost = argv[++i];
        }
        else if (arg == "--queue" && hasValue)
        {
            const std::string kind = argv[++i];
            if (kind == "spsc")
            {
                options.exchange.queue = agora::net::QueueKind::Spsc;
            }
            else if (kind == "mutex")
            {
                options.exchange.queue = agora::net::QueueKind::Mutex;
            }
            else
            {
                return false;
            }
        }
        else if (arg == "--seed" && hasValue)
        {
            options.exchange.seed = std::stoull(argv[++i]);
        }
        else if (arg == "--agents" && hasValue)
        {
            options.exchange.agents = std::stoull(argv[++i]);
        }
        else if (arg == "--ticks" && hasValue)
        {
            options.exchange.ticks = std::stoull(argv[++i]);
        }
        else if (arg == "--tick-ms" && hasValue)
        {
            options.exchange.tickMillis = std::stoi(argv[++i]);
        }
        else if (arg == "--lockstep")
        {
            options.exchange.lockstep = true;
        }
        else if (arg == "--trades" && hasValue)
        {
            options.tradesPath = argv[++i];
        }
        else if (arg == "--commands" && hasValue)
        {
            options.commandsPath = argv[++i];
        }
        else if (arg == "--tick-log" && hasValue)
        {
            options.tickLogPath = argv[++i];
        }
        else if (arg == "--verify")
        {
            options.exchange.checkInvariants = true;
        }
        else
        {
            return false;
        }
    }
    return options.exchange.tickMillis >= 0;
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
        std::ofstream tradesFile;
        if (!options.tradesPath.empty())
        {
            tradesFile.open(options.tradesPath, std::ios::binary);
            if (!tradesFile)
            {
                std::cerr << "cannot write " << options.tradesPath << '\n';
                return 1;
            }
            options.exchange.tradeLog = &tradesFile;
        }
        std::ofstream commandsFile;
        if (!options.commandsPath.empty())
        {
            commandsFile.open(options.commandsPath, std::ios::binary);
            if (!commandsFile)
            {
                std::cerr << "cannot write " << options.commandsPath << '\n';
                return 1;
            }
            options.exchange.commandLog = &commandsFile;
        }

        std::ofstream tickLogFile;
        if (!options.tickLogPath.empty())
        {
            tickLogFile.open(options.tickLogPath, std::ios::binary);
            if (!tickLogFile)
            {
                std::cerr << "cannot write " << options.tickLogPath << '\n';
                return 1;
            }
            options.exchange.tickLog = &tickLogFile;
        }

        agora::net::ExchangeServer server(options.exchange);
        server.start();

        g_server.store(&server);
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        std::cout << "agora_exchange listening on " << options.exchange.gateway.bindHost << ':' << server.port()
                  << " (" << (options.exchange.lockstep ? "lockstep" : "live") << ", queue "
                  << server.queueName() << ", seed " << options.exchange.seed << ")" << std::endl;

        server.run();
        g_server.store(nullptr);

        // The session is over and the engine thread (this one) is idle: check everything once more.
        server.engine().verify();

        const agora::EngineStats& stats = server.engine().stats();
        std::cout << "ticks " << server.ticks() << ", commands " << stats.commands << ", accepted "
                  << stats.accepted << ", rejected " << stats.rejected << ", cancels " << stats.cancels
                  << ", trades " << stats.trades << ", traded volume " << stats.tradedVolume
                  << ", resting orders " << server.engine().book().orderCount()
                  << "\ninvariants and volume conservation: OK\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
