// End-to-end tests over real TCP sockets on the loopback interface: the
// exchange runs on one thread, clients and agents on others.
#include "agora/agents/agent_factory.hpp"
#include "agora/core/matching_engine.hpp"
#include "agora/io/command_log.hpp"
#include "agora/io/trade_log.hpp"
#include "agora/net/client_connection.hpp"
#include "agora/net/exchange_server.hpp"

#include "doctest/doctest.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace agora;
using namespace agora::net;

namespace
{

/// An exchange on an OS-chosen loopback port, with its engine loop on a background thread.
class TestExchange
{
  public:
    explicit TestExchange(ExchangeConfig config)
    {
        config.gateway.port = 0;
        config.tradeLog = &trades;
        config.commandLog = &commands;
        config.tickLog = &ticks;
        m_server = std::make_unique<ExchangeServer>(config);
        m_server->start();
        m_thread = std::thread([this] { m_server->run(); });
    }

    ~TestExchange() { stop(); }

    std::uint16_t port() const { return m_server->port(); }

    /// Waits for run() to return on its own (fixed number of ticks).
    void join()
    {
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void stop()
    {
        m_server->requestStop();
        join();
    }

    const MatchingEngine& engine() const { return m_server->engine(); }

    std::ostringstream trades;
    std::ostringstream commands;
    std::ostringstream ticks;

  private:
    std::unique_ptr<ExchangeServer> m_server;
    std::thread m_thread;
};

/// Connects and completes the handshake as @p agent.
std::unique_ptr<ClientConnection> connectAs(std::uint16_t port, AgentId agent, WelcomeStatus expected)
{
    auto connection = std::make_unique<ClientConnection>("127.0.0.1", port);
    Hello hello;
    hello.agent = agent;
    connection->send(hello);
    REQUIRE(connection->flush());

    ServerMessage message;
    REQUIRE(connection->receive(message));
    REQUIRE(std::holds_alternative<Welcome>(message));
    CHECK(std::get<Welcome>(message).status == expected);
    return connection;
}

/// Receives the next message and requires it to be of type Message.
template <typename Message> Message expect(ClientConnection& connection)
{
    ServerMessage message;
    REQUIRE(connection.receive(message));
    REQUIRE(std::holds_alternative<Message>(message));
    return std::get<Message>(message);
}

/// Feeds a recorded command log to a fresh engine and returns its trade log.
std::string replay(const std::string& commandLog)
{
    std::istringstream input(commandLog);
    CommandLogReader reader(input);
    std::ostringstream trades;
    TradeLogWriter tradeLog(trades);
    MatchingEngine engine(tradeLog, EngineConfig{true});
    Command command;
    while (reader.read(command))
    {
        engine.process(command);
    }
    engine.verify();
    return trades.str();
}

/// Everything a finished session left behind.
struct SessionResult
{
    std::string trades;
    std::string commands;
    std::string ticks;
    EngineStats stats;
};

/// Runs a whole market: one exchange and one agent thread per entry of @p types.
SessionResult runMarket(std::uint64_t seed, const std::vector<std::string>& types, std::uint64_t ticks,
                        bool lockstep, QueueKind queue)
{
    const std::size_t agentCount = types.size();
    ExchangeConfig config;
    config.seed = seed;
    config.queue = queue;
    config.lockstep = lockstep;
    config.agents = agentCount;
    config.ticks = ticks;
    config.tickMillis = 1;
    config.checkInvariants = true;
    TestExchange exchange(config);

    std::vector<std::thread> agents;
    for (std::size_t i = 0; i < agentCount; ++i)
    {
        agents.emplace_back(
            [&exchange, &types, seed, i]
            {
                const std::unique_ptr<agents::Agent> agent =
                    agents::makeAgent(types[i], static_cast<AgentId>(i + 1), seed);
                ClientConnection connection("127.0.0.1", exchange.port());
                agents::AgentRunner runner(*agent, connection);
                runner.run();
            });
    }
    for (std::thread& agent : agents)
    {
        agent.join();
    }
    exchange.join();
    exchange.engine().verify();
    return SessionResult{exchange.trades.str(), exchange.commands.str(), exchange.ticks.str(),
                         exchange.engine().stats()};
}

/// Convenience overload: @p agentCount zero-intelligence agents.
SessionResult runMarket(std::uint64_t seed, std::size_t agentCount, std::uint64_t ticks, bool lockstep,
                        QueueKind queue)
{
    return runMarket(seed, std::vector<std::string>(agentCount, "zi"), ticks, lockstep, queue);
}

} // namespace

TEST_CASE("exchange: two clients trade over TCP (both queue kinds)")
{
    for (const QueueKind queue : {QueueKind::Spsc, QueueKind::Mutex})
    {
        ExchangeConfig config;
        config.queue = queue;
        config.tickMillis = 0; // no ticks, so the message sequence below is exact
        TestExchange exchange(config);

        const auto seller = connectAs(exchange.port(), 1, WelcomeStatus::Ok);
        const auto buyer = connectAs(exchange.port(), 2, WelcomeStatus::Ok);

        seller->send(NewOrder{10, Side::Sell, OrderType::Limit, 100, 5});
        REQUIRE(seller->flush());
        const Accepted resting = expect<Accepted>(*seller);
        CHECK(resting.clientOrderId == 10);
        CHECK(resting.price == 100);

        // The buyer crosses for part of the resting quantity and trades at the maker's price.
        buyer->send(NewOrder{20, Side::Buy, OrderType::Limit, 105, 3});
        REQUIRE(buyer->flush());
        const Accepted taking = expect<Accepted>(*buyer);
        CHECK(taking.clientOrderId == 20);

        const Trade buyerTrade = expect<Trade>(*buyer);
        const Trade sellerTrade = expect<Trade>(*seller);
        CHECK(buyerTrade.tradeId == sellerTrade.tradeId);
        CHECK(buyerTrade.price == 100);
        CHECK(buyerTrade.quantity == 3);
        CHECK(buyerTrade.buyAgent == 2);
        CHECK(buyerTrade.sellAgent == 1);
        CHECK(buyerTrade.sellOrderId == resting.orderId);
        CHECK(buyerTrade.aggressor == Side::Buy);

        // Only the owner can cancel: the buyer is refused, the seller succeeds.
        buyer->send(CancelOrder{21, resting.orderId});
        REQUIRE(buyer->flush());
        CHECK(expect<Rejected>(*buyer).reason == RejectReason::NotOwner);

        seller->send(CancelOrder{11, resting.orderId});
        REQUIRE(seller->flush());
        const Cancelled cancelled = expect<Cancelled>(*seller);
        CHECK(cancelled.quantity == 2);
        CHECK(cancelled.reason == CancelReason::Requested);

        exchange.stop();
        expect<End>(*seller);
        expect<End>(*buyer);
        CHECK(exchange.engine().stats().trades == 1);
        CHECK(exchange.engine().book().empty());

        // The recorded commands reproduce the trade log exactly.
        CHECK(replay(exchange.commands.str()) == exchange.trades.str());
    }
}

TEST_CASE("exchange: handshake refuses duplicate agent ids and wrong versions")
{
    ExchangeConfig config;
    config.tickMillis = 0;
    TestExchange exchange(config);

    const auto first = connectAs(exchange.port(), 7, WelcomeStatus::Ok);
    const auto second = connectAs(exchange.port(), 7, WelcomeStatus::DuplicateAgent);
    ServerMessage message;
    CHECK_FALSE(second->receive(message)); // the exchange hung up

    ClientConnection old("127.0.0.1", exchange.port());
    Hello hello;
    hello.version = kProtocolVersion + 1;
    hello.agent = 8;
    old.send(hello);
    REQUIRE(old.flush());
    CHECK(expect<Welcome>(old).status == WelcomeStatus::BadVersion);
    CHECK_FALSE(old.receive(message));

    // The first client is unaffected, and its id is free again after it leaves.
    first->send(NewOrder{1, Side::Buy, OrderType::Limit, 50, 1});
    REQUIRE(first->flush());
    expect<Accepted>(*first);
}

TEST_CASE("exchange: a client that sends garbage is dropped without hurting others")
{
    ExchangeConfig config;
    config.tickMillis = 0;
    TestExchange exchange(config);

    const auto good = connectAs(exchange.port(), 1, WelcomeStatus::Ok);

    Socket bad = Socket::connectTo("127.0.0.1", exchange.port());
    const unsigned char garbage[] = {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4};
    REQUIRE(bad.sendAll(garbage, sizeof(garbage)));
    unsigned char byte = 0;
    CHECK(bad.receive(&byte, 1) <= 0); // closed by the exchange

    good->send(NewOrder{1, Side::Sell, OrderType::Limit, 70, 1});
    REQUIRE(good->flush());
    CHECK(expect<Accepted>(*good).price == 70);
}

TEST_CASE("exchange: resting orders are cancelled when their owner disconnects")
{
    ExchangeConfig config;
    config.tickMillis = 0;
    TestExchange exchange(config);

    auto maker = connectAs(exchange.port(), 1, WelcomeStatus::Ok);
    const auto taker = connectAs(exchange.port(), 2, WelcomeStatus::Ok);

    maker->send(NewOrder{1, Side::Sell, OrderType::Limit, 100, 1'000'000});
    REQUIRE(maker->flush());
    expect<Accepted>(*maker);
    maker->close();
    maker.reset();

    // The disconnect is handled asynchronously, so probe with 1-share market
    // orders until one finds the book empty.
    bool bookEmptied = false;
    for (std::uint64_t attempt = 0; attempt < 400 && !bookEmptied; ++attempt)
    {
        taker->send(NewOrder{attempt, Side::Buy, OrderType::Market, 0, 1});
        REQUIRE(taker->flush());
        expect<Accepted>(*taker);
        ServerMessage outcome;
        REQUIRE(taker->receive(outcome));
        if (std::holds_alternative<Cancelled>(outcome))
        {
            CHECK(std::get<Cancelled>(outcome).reason == CancelReason::NoLiquidity);
            bookEmptied = true;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(bookEmptied);
    exchange.stop();
    CHECK(exchange.engine().book().empty());
}

TEST_CASE("exchange: lockstep sessions are deterministic (result 6)")
{
    const SessionResult first = runMarket(42, 6, 400, true, QueueKind::Spsc);
    const SessionResult second = runMarket(42, 6, 400, true, QueueKind::Spsc);

    // The market actually did something...
    CHECK(first.stats.trades > 50);
    CHECK(first.stats.cancels > 0);
    // ...and did exactly the same thing twice, byte for byte.
    CHECK(first.trades == second.trades);
    CHECK(first.commands == second.commands);

    SUBCASE("the queue implementation does not change the outcome")
    {
        const SessionResult withMutex = runMarket(42, 6, 400, true, QueueKind::Mutex);
        CHECK(withMutex.trades == first.trades);
    }
    SUBCASE("a different seed gives a different market")
    {
        const SessionResult other = runMarket(43, 6, 400, true, QueueKind::Spsc);
        CHECK(other.trades != first.trades);
    }
    SUBCASE("replaying the command log reproduces the trade log")
    {
        CHECK(replay(first.commands) == first.trades);
    }
}

TEST_CASE("exchange: a live session replays to the same trade log (result 6)")
{
    // Live arrival order is up to the OS scheduler, so two live runs differ.
    // What must hold is that the recorded command stream explains the trades.
    const SessionResult live = runMarket(7, 4, 150, false, QueueKind::Spsc);
    CHECK(live.stats.commands > 0);
    CHECK(replay(live.commands) == live.trades);
}

TEST_CASE("exchange: a mixed market of all four strategies is deterministic in lockstep")
{
    const std::vector<std::string> mix = {"mm", "mm", "zi", "zi", "zi", "value", "value", "trend", "trend"};
    const SessionResult first = runMarket(11, mix, 600, true, QueueKind::Spsc);
    const SessionResult second = runMarket(11, mix, 600, true, QueueKind::Mutex);

    CHECK(first.stats.trades > 100);
    CHECK(first.trades == second.trades);
    CHECK(replay(first.commands) == first.trades);

    // The tick log is deterministic too, with a header and one line per tick.
    CHECK(first.ticks == second.ticks);
    CHECK(std::count(first.ticks.begin(), first.ticks.end(), '\n') == 601);
}
