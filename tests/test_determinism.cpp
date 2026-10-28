// Randomized invariant checks, command log round trip and replay determinism (result 6).
#include "agora/core/matching_engine.hpp"
#include "agora/core/random_order_flow.hpp"
#include "agora/io/command_log.hpp"
#include "agora/io/trade_log.hpp"

#include "doctest/doctest.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using agora::Command;
using agora::MatchingEngine;

namespace
{

/// Result of one generated session.
struct Session
{
    std::vector<Command> commands;
    std::string tradeCsv;
    agora::EngineStats stats;
};

/// Runs RandomOrderFlow against an engine and records commands and trades.
Session generate(std::uint64_t seed, std::size_t count, bool checkEveryCommand)
{
    agora::FlowConfig config;
    config.seed = seed;
    agora::RandomOrderFlow flow(config);

    std::ostringstream trades;
    agora::TradeLogWriter tradeLog(trades);
    agora::ListenerFanout fanout;
    fanout.add(flow);
    fanout.add(tradeLog);

    agora::EngineConfig engineConfig;
    engineConfig.checkInvariantsEveryCommand = checkEveryCommand;
    MatchingEngine engine(fanout, engineConfig);

    Session session;
    session.commands.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        session.commands.push_back(flow.next());
        engine.process(session.commands.back());
    }
    engine.verify();
    session.tradeCsv = trades.str();
    session.stats = engine.stats();
    return session;
}

/// Feeds a fixed command list to a fresh engine and returns the trade CSV.
std::string replay(const std::vector<Command>& commands)
{
    std::ostringstream trades;
    agora::TradeLogWriter tradeLog(trades);
    MatchingEngine engine(tradeLog);
    for (const Command& command : commands)
    {
        engine.process(command);
    }
    engine.verify();
    return trades.str();
}

} // namespace

TEST_CASE("random flow keeps every invariant after every command")
{
    // checkInvariantsEveryCommand makes process() throw on any violation.
    for (std::uint64_t seed : {1ULL, 2ULL, 3ULL})
    {
        CAPTURE(seed);
        const Session session = generate(seed, 20000, true);
        CHECK(session.stats.trades > 500);  // the flow actually trades
        CHECK(session.stats.rejected > 0);   // and exercises the reject path
        CHECK(session.stats.cancels > 500); // and the cancel path
    }
}

TEST_CASE("same seed gives the same commands and byte-identical trades")
{
    const Session a = generate(42, 50000, false);
    const Session b = generate(42, 50000, false);
    CHECK(a.commands == b.commands);
    CHECK(a.tradeCsv == b.tradeCsv);

    const Session c = generate(43, 50000, false);
    CHECK(a.tradeCsv != c.tradeCsv);
}

TEST_CASE("replaying recorded commands reproduces the trade log exactly")
{
    const Session live = generate(7, 50000, false);
    CHECK(replay(live.commands) == live.tradeCsv);
}

TEST_CASE("command log round trip through the binary format")
{
    const Session live = generate(11, 10000, false);

    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    {
        agora::CommandLogWriter writer(buffer);
        for (const Command& command : live.commands)
        {
            writer.write(command);
        }
        CHECK(writer.count() == live.commands.size());
    }
    CHECK(buffer.str().size() == 12 + live.commands.size() * agora::kCommandRecordSize);

    agora::CommandLogReader reader(buffer);
    std::vector<Command> decoded;
    Command command;
    while (reader.read(command))
    {
        decoded.push_back(command);
    }
    CHECK(decoded == live.commands);
    CHECK(replay(decoded) == live.tradeCsv);
}

TEST_CASE("command log rejects bad headers and truncated records")
{
    std::istringstream notALog("definitely not a log");
    CHECK_THROWS_AS(agora::CommandLogReader{notALog}, std::runtime_error);

    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    {
        agora::CommandLogWriter writer(buffer);
        writer.write(Command::newLimit(1, 1, agora::Side::Buy, 100, 5));
    }
    std::string bytes = buffer.str();
    bytes.pop_back();
    std::istringstream truncated(bytes);
    agora::CommandLogReader reader(truncated);
    Command command;
    CHECK_THROWS_AS(reader.read(command), std::runtime_error);
}

TEST_CASE("rng is reproducible and in range")
{
    agora::Rng a(123);
    agora::Rng b(123);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK(a.next() == b.next());
    }
    agora::Rng r(5);
    for (int i = 0; i < 10000; ++i)
    {
        const auto v = r.between(-3, 3);
        CHECK(v >= -3);
        CHECK(v <= 3);
        const double u = r.uniform();
        CHECK(u >= 0.0);
        CHECK(u < 1.0);
    }
}
