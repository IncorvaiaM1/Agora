/**
 * @file exchange_server.hpp
 * @brief ExchangeServer: the whole exchange, from sockets to matching engine.
 */
#pragma once

#include "agora/core/matching_engine.hpp"
#include "agora/core/rng.hpp"
#include "agora/io/command_log.hpp"
#include "agora/io/trade_log.hpp"
#include "agora/net/command_channel.hpp"
#include "agora/net/gateway.hpp"
#include "agora/net/market_data_feed.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <unordered_map>
#include <vector>

namespace agora::net
{

/// Exchange settings.
struct ExchangeConfig
{
    GatewayConfig gateway;
    QueueKind queue = QueueKind::Spsc; ///< Session-to-engine queue (result 2 compares the two).
    std::size_t queueCapacity = 1024;  ///< Messages per session ring.
    std::uint64_t seed = 42;           ///< Seeds the fundamental value and the lockstep ordering.

    /// Lockstep mode: deterministic simulation clock (see ExchangeServer).
    bool lockstep = false;
    /// Trading agents to wait for before the first tick. Required (> 0) in lockstep mode.
    std::size_t agents = 0;
    /// Stop after this many ticks. 0 means run until requestStop() (live mode only).
    std::uint64_t ticks = 0;
    /// Live mode: milliseconds between ticks. 0 disables ticks (pure order entry, for benchmarks).
    int tickMillis = 10;

    Price initialFundamental = 10'000; ///< Starting fundamental value in ticks.
    Price fundamentalStep = 3;         ///< The fundamental moves by a uniform draw in [-step, +step] per tick.

    bool checkInvariants = false;       ///< Verify book invariants and conservation after every command.
    std::ostream* tradeLog = nullptr;   ///< If set, receives the trade log CSV.
    std::ostream* commandLog = nullptr; ///< If set, receives the binary command log for agora_replay.
    /// If set, receives one CSV line per tick: the fundamental value and the top of the
    /// book as the agents saw them. Input to the market statistics (result 3).
    std::ostream* tickLog = nullptr;
};

/**
 * @brief A complete exchange: Gateway, CommandChannel, MatchingEngine and MarketDataFeed.
 *
 * Threads: one acceptor and one reader per client (inside Gateway) feed the
 * CommandChannel; the thread that calls run() is the engine thread. It is
 * the only thread that touches the MatchingEngine and the only one that
 * writes to client sockets, so the engine itself needs no locks.
 *
 * Two modes:
 *
 *  - **Live.** Commands are matched in the order they come off the channel,
 *    which depends on OS scheduling. Fast and realistic; used for the
 *    latency and throughput results. Not reproducible run to run, but the
 *    command log replays to a byte-identical trade log.
 *
 *  - **Lockstep.** A simulation clock. Each tick the exchange sends Tick to
 *    every agent, buffers their orders until all of them answer TickDone,
 *    then matches the buffered orders agent by agent in an order drawn from
 *    the seeded RNG. Arrival timing no longer matters, so the same seed
 *    gives a byte-identical trade log on every run (result 6).
 */
class ExchangeServer final : private EventListener
{
  public:
    explicit ExchangeServer(ExchangeConfig config);
    ~ExchangeServer() override;

    ExchangeServer(const ExchangeServer&) = delete;
    ExchangeServer& operator=(const ExchangeServer&) = delete;

    /// Binds the port and starts accepting clients. @throws std::runtime_error.
    void start();

    /**
     * @brief Runs the engine loop on the calling thread until the session ends.
     *
     * Returns after the configured number of ticks, after requestStop(), or
     * (lockstep) when every agent has left. Clients receive End and are
     * disconnected before this returns.
     */
    void run();

    /// Asks run() to return soon. Safe from any thread and from a signal handler.
    void requestStop() noexcept { m_stop.store(true, std::memory_order_release); }

    /// The bound port; valid after start().
    std::uint16_t port() const noexcept { return m_gateway.port(); }

    /// Engine state for reporting and verification. Call only after run() returned.
    const MatchingEngine& engine() const noexcept { return m_engine; }

    /// Ticks published so far. Call only after run() returned.
    std::uint64_t ticks() const noexcept { return m_tick; }

    /// "mutex" or "spsc".
    const char* queueName() const noexcept { return m_channel->name(); }

  private:
    // EventListener: route each engine event to the sessions that should see it.
    void onAccepted(const AcceptedEvent& event) override;
    void onRejected(const RejectedEvent& event) override;
    void onCancelled(const CancelledEvent& event) override;
    void onTrade(const TradeEvent& event) override;

    void runLive();
    void runLockstep();

    /// Handles messages until the configured number of agents is connected. @return false if stopped.
    bool waitForAgents();

    void handle(const InboundMessage& message);
    void handleConnect(const InboundMessage& message);
    void handleDisconnect(const InboundMessage& message);
    void handleOrder(const InboundMessage& message);

    /// Logs (if enabled) and matches one command.
    void submit(const Command& command);

    /// Cancels whatever @p agent still has resting, so a dead client leaves no stale quotes.
    void cancelOpenOrders(AgentId agent);

    /// Advances the clock and the fundamental value and publishes Tick.
    void publishTick();

    /// Session of a trading agent, or nullptr if it is not connected.
    Session* sessionOf(AgentId agent);

    /// Resting-order bookkeeping for one agent (for cancel on disconnect).
    struct OpenOrders
    {
        std::vector<OrderId> ids;        ///< Every limit order accepted; may include finished ones.
        std::size_t compactAt = 1024;    ///< Prune finished ids when the list reaches this size.
    };

    ExchangeConfig m_config;
    std::unique_ptr<CommandChannel> m_channel;
    Gateway m_gateway;
    std::unique_ptr<TradeLogWriter> m_tradeLog;
    std::unique_ptr<CommandLogWriter> m_commandLog;
    ListenerFanout m_fanout;
    MatchingEngine m_engine;
    MarketDataFeed m_feed;

    Rng m_fundamentalRng; ///< Drives the fundamental value only, so its path depends on the seed alone.
    Rng m_orderRng;       ///< Shuffles the per-tick agent order in lockstep mode.
    Price m_fundamental;
    std::uint64_t m_tick = 0;
    bool m_ticking = false; ///< Lockstep: the first tick has been published.

    std::unordered_map<AgentId, std::uint32_t> m_agentSlots; ///< Connected trading agents.
    std::unordered_map<AgentId, OpenOrders> m_openOrders;

    // Lockstep state. Ordered containers so iteration order is defined.
    std::map<AgentId, std::vector<Command>> m_pending; ///< Orders buffered during the current tick.
    std::set<AgentId> m_waiting;                       ///< Agents that have not sent TickDone yet.
    std::vector<AgentId> m_departed;                   ///< Agents that left during the current tick.

    std::atomic<bool> m_stop{false};
};

} // namespace agora::net
