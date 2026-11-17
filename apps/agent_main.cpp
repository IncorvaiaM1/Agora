/**
 * @file agent_main.cpp
 * @brief agora_agent: one trading agent as its own process.
 *
 * Usage:
 *   agora_agent --id N [--type zi|value|trend|mm] [--seed S] [--host H] [--port P]
 *
 * Connects to the exchange, trades until the exchange ends the session, then
 * prints one summary line. Every agent in a session should get the same
 * --seed and a different --id; the id is mixed into the seed so their random
 * streams differ.
 */
#include "agora/agents/agent_factory.hpp"
#include "agora/net/client_connection.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace
{

struct Options
{
    std::string type = "zi";
    agora::AgentId id = 0;
    bool hasId = false;
    std::uint64_t seed = 42;
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
};

void printUsage()
{
    std::cerr << "usage: agora_agent --id N [--type " << agora::agents::kAgentTypes
              << "] [--seed S] [--host H] [--port P]\n";
}

bool parse(int argc, char* argv[], Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--type" && hasValue)
        {
            options.type = argv[++i];
        }
        else if (arg == "--id" && hasValue)
        {
            options.id = static_cast<agora::AgentId>(std::stoul(argv[++i]));
            options.hasId = true;
        }
        else if (arg == "--seed" && hasValue)
        {
            options.seed = std::stoull(argv[++i]);
        }
        else if (arg == "--host" && hasValue)
        {
            options.host = argv[++i];
        }
        else if (arg == "--port" && hasValue)
        {
            options.port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        }
        else
        {
            return false;
        }
    }
    return options.hasId;
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
        const std::unique_ptr<agora::agents::Agent> agent =
            agora::agents::makeAgent(options.type, options.id, options.seed);
        if (!agent)
        {
            std::cerr << "unknown agent type: " << options.type << '\n';
            return 2;
        }

        agora::net::ClientConnection connection(options.host, options.port);
        agora::agents::AgentRunner runner(*agent, connection);
        const agora::agents::RunSummary summary = runner.run();

        std::cout << "agent " << agent->id() << " (" << agent->name() << "): ticks " << summary.ticks
                  << ", orders " << summary.ordersSent << ", cancels " << summary.cancelsSent << ", fills "
                  << agent->tradeCount() << ", position " << agent->position() << ", cash " << agent->cash()
                  << ", mark-to-market " << agent->markToMarket(summary.lastPrice)
                  << (summary.endedCleanly ? "" : " [connection lost]") << '\n';
        return summary.endedCleanly ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
