/**
 * @file value_agent.hpp
 * @brief ValueAgent: a fundamentalist that trades toward its estimate of fair value.
 */
#pragma once

#include "agora/agents/agent.hpp"

namespace agora::agents
{

/// Parameters of the value strategy.
struct ValueConfig
{
    double actProbability = 0.3;   ///< Chance of considering a trade in a given tick.
    Price maxBias = 10;            ///< Each agent's fixed misjudgement of value is uniform in [-maxBias, +maxBias].
    Price noise = 15;              ///< Fresh per-tick observation noise, uniform in [-noise, +noise].
    Price margin = 8;              ///< Required edge: trade only if the price is this far from the estimate.
    Quantity maxQuantity = 20;     ///< Largest single order.
    Quantity positionLimit = 2000;  ///< Stops adding to a position beyond this many shares.
    std::uint64_t orderLifetime = 25; ///< Unfilled orders are cancelled after this many ticks.
};

/**
 * @brief Fundamentalist trader.
 *
 * It sees the exchange's fundamental value through noise, and when the
 * market price is further than a margin from its estimate it places a limit
 * order priced at the edge of that margin: a buy below value, a sell above.
 * Orders are larger when the mispricing is larger. Value agents pull the
 * price back toward the fundamental and so supply the mean reversion that
 * trend followers work against.
 */
class ValueAgent final : public Agent
{
  public:
    ValueAgent(AgentId id, std::uint64_t seed, ValueConfig config = {});

    const char* name() const noexcept override { return "value"; }

    /// This agent's fixed bias in ticks (exposed for tests).
    Price bias() const noexcept { return m_bias; }

  protected:
    void onTick(const net::Tick& tick, OrderSink& orders) override;

  private:
    ValueConfig m_config;
    Price m_bias;
};

} // namespace agora::agents
