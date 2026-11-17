/**
 * @file value_agent.cpp
 * @brief Implementation of ValueAgent.
 */
#include "agora/agents/value_agent.hpp"

#include <algorithm>

namespace agora::agents
{

ValueAgent::ValueAgent(AgentId id, std::uint64_t seed, ValueConfig config)
    : Agent(id, seed), m_config(config), m_bias(rng().between(-config.maxBias, config.maxBias))
{
}

void ValueAgent::onTick(const net::Tick& tick, OrderSink& orders)
{
    // Stale orders were priced off an old estimate; take them down.
    cancelOlderThan(m_config.orderLifetime, orders);

    if (!rng().chance(m_config.actProbability))
    {
        return;
    }

    const Price estimate = tick.fundamental + m_bias + rng().between(-m_config.noise, m_config.noise);
    const Price market = referencePrice(tick);
    const Price gap = estimate - market; // positive: the market looks cheap

    if (gap > m_config.margin && position() < m_config.positionLimit)
    {
        // Willing to pay up to the estimate less the margin. That is above the
        // market, so the order takes what is offered and rests the remainder.
        const Quantity quantity = std::clamp<Quantity>(gap / m_config.margin, 1, m_config.maxQuantity);
        orders.limit(Side::Buy, std::max<Price>(estimate - m_config.margin, 1), quantity);
    }
    else if (gap < -m_config.margin && position() > -m_config.positionLimit)
    {
        const Quantity quantity = std::clamp<Quantity>(-gap / m_config.margin, 1, m_config.maxQuantity);
        orders.limit(Side::Sell, estimate + m_config.margin, quantity);
    }
}

} // namespace agora::agents
