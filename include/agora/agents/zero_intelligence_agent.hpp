/**
 * @file zero_intelligence_agent.hpp
 * @brief ZeroIntelligenceAgent: random order flow with no strategy at all.
 */
#pragma once

#include "agora/agents/agent.hpp"

namespace agora::agents
{

/// Parameters of the zero-intelligence strategy.
struct ZeroIntelligenceConfig
{
    double actProbability = 0.6;    ///< Chance of doing anything in a given tick.
    double marketProbability = 0.1; ///< Of the actions, share that are market orders.
    double cancelProbability = 0.2; ///< Of the actions, share that cancel the oldest open order.
    Price depth = 20;               ///< Limit prices reach this many ticks behind the reference price.
    Price aggression = 5;           ///< ... and this many ticks through it (these orders cross).
    Quantity maxQuantity = 10;      ///< Order sizes are uniform in [1, maxQuantity].
    std::size_t maxOpenOrders = 20; ///< Above this the agent cancels before adding more.
};

/**
 * @brief The classic zero-intelligence trader (Gode and Sunder; Farmer, Patelli and Zovko).
 *
 * Each tick it flips coins: do nothing, cancel its oldest order, send a
 * market order, or send a limit order at a price drawn uniformly from a
 * window around the current mid price. It has no view on value and never
 * looks at its own position. It is the baseline for result 3: whatever
 * structure shows up in a market of these agents comes from the order book
 * mechanism itself, not from strategy.
 */
class ZeroIntelligenceAgent final : public Agent
{
  public:
    ZeroIntelligenceAgent(AgentId id, std::uint64_t seed, ZeroIntelligenceConfig config = {});

    const char* name() const noexcept override { return "zi"; }
    void onTick(const net::Tick& tick, OrderSink& orders) override;

  private:
    /// Mid price if both sides are quoted, otherwise the best information available.
    static Price referencePrice(const net::Tick& tick) noexcept;

    ZeroIntelligenceConfig m_config;
};

} // namespace agora::agents
