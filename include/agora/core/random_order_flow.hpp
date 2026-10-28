/**
 * @file random_order_flow.hpp
 * @brief RandomOrderFlow: seeded synthetic command stream for tests and benchmarks.
 *
 * This is not one of the market agents (those live in src/agents and talk to
 * the exchange over sockets). It is an in-process load generator that drives
 * the engine directly, so the core can be fuzzed and benchmarked without any
 * networking.
 */
#pragma once

#include "agora/core/command.hpp"
#include "agora/core/events.hpp"
#include "agora/core/rng.hpp"

#include <unordered_map>
#include <utility>
#include <vector>

namespace agora
{

/// Parameters of the synthetic order flow.
struct FlowConfig
{
    std::uint64_t seed = 1;
    AgentId agents = 8;                  ///< Agent ids are 1..agents.
    Price startMid = 10000;              ///< Initial mid price in ticks.
    Price maxOffset = 20;                ///< Limit prices are within this many ticks of mid.
    Quantity maxQuantity = 100;          ///< Order sizes are uniform in [1, maxQuantity].
    double cancelProbability = 0.25;     ///< Chance a command is a cancel (if anything is resting).
    double aggressiveProbability = 0.25; ///< Chance a limit order is priced through mid (likely to trade).
    double marketProbability = 0.05;     ///< Chance a new order is a market order.
    double driftProbability = 0.10;      ///< Chance per command that the mid moves one tick.
    double invalidProbability = 0.01;    ///< Chance a command is deliberately invalid.
};

/**
 * @brief Generates a reproducible mix of limit orders, market orders and cancels.
 *
 * Register it as an EventListener on the engine (directly or through a
 * ListenerFanout) so it learns the exchange ids of accepted orders and
 * tracks their fills, and only issues cancels for orders still resting.
 * A small fraction of commands are deliberately invalid
 * (FlowConfig::invalidProbability) to keep the reject path exercised.
 */
class RandomOrderFlow final : public EventListener
{
  public:
    explicit RandomOrderFlow(const FlowConfig& config);

    /// Draws the next command.
    Command next();

    void onAccepted(const AcceptedEvent& event) override;
    void onTrade(const TradeEvent& event) override;
    void onCancelled(const CancelledEvent& event) override;

    Price mid() const noexcept { return m_mid; }

  private:
    /// Subtracts a fill from a tracked order and forgets it once fully filled.
    void reduce(OrderId id, Quantity quantity);

    FlowConfig m_config;
    Rng m_rng;
    Price m_mid;
    ClientOrderId m_nextClientId = 1;

    /// Resting orders this flow knows about: id -> (owner, remaining quantity).
    std::unordered_map<OrderId, std::pair<AgentId, Quantity>> m_live;
    /// Ids to draw cancels from. May hold ids that have since left the book;
    /// those are skipped lazily when drawn, which keeps every operation O(1).
    std::vector<OrderId> m_cancelCandidates;
};

} // namespace agora
