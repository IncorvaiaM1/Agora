/**
 * @file replay_main.cpp
 * @brief agora_replay: replays a recorded command log through a fresh engine.
 *
 * Usage:
 *   agora_replay <commands.bin> [--trades <out.csv>] [--verify]
 *
 * With --trades the resulting trade log is written as CSV. Replaying the same
 * command log always produces a byte-identical trade log; compare two runs
 * with `cmp` or `sha256sum`. With --verify the book invariants and volume
 * conservation are checked after every command.
 */
#include "agora/core/matching_engine.hpp"
#include "agora/io/command_log.hpp"
#include "agora/io/trade_log.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace
{

void printUsage()
{
    std::cerr << "usage: agora_replay <commands.bin> [--trades <out.csv>] [--verify]\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printUsage();
        return 2;
    }

    const std::string inputPath = argv[1];
    std::string tradesPath;
    bool verifyEachCommand = false;
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--trades" && i + 1 < argc)
        {
            tradesPath = argv[++i];
        }
        else if (arg == "--verify")
        {
            verifyEachCommand = true;
        }
        else
        {
            printUsage();
            return 2;
        }
    }

    try
    {
        std::ifstream input(inputPath, std::ios::binary);
        if (!input)
        {
            std::cerr << "cannot open " << inputPath << '\n';
            return 1;
        }
        agora::CommandLogReader reader(input);

        std::ofstream tradesFile;
        std::unique_ptr<agora::TradeLogWriter> tradeLog;
        agora::NullListener nullListener;
        agora::EventListener* listener = &nullListener;
        if (!tradesPath.empty())
        {
            tradesFile.open(tradesPath, std::ios::binary);
            if (!tradesFile)
            {
                std::cerr << "cannot write " << tradesPath << '\n';
                return 1;
            }
            tradeLog = std::make_unique<agora::TradeLogWriter>(tradesFile);
            listener = tradeLog.get();
        }

        agora::EngineConfig config;
        config.checkInvariantsEveryCommand = verifyEachCommand;
        agora::MatchingEngine engine(*listener, config);

        agora::Command command;
        while (reader.read(command))
        {
            engine.process(command);
        }
        engine.verify();

        const agora::EngineStats& stats = engine.stats();
        std::cout << "commands " << stats.commands << ", accepted " << stats.accepted << ", rejected "
                  << stats.rejected << ", cancels " << stats.cancels << ", trades " << stats.trades
                  << ", traded volume " << stats.tradedVolume << ", resting orders "
                  << engine.book().orderCount() << "\ninvariants and volume conservation: OK\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
