/**
 * @file trend_agent.cpp
 * @brief Implementation of TrendAgent.
 */
#include "agora/agents/trend_agent.hpp"

#include <algorithm>

namespace agora::agents
{

TrendAgent::TrendAgent(AgentId id, std::uint64_t seed, TrendConfig config)
    : Agent(id, seed), m_config(config),
      m_fastWindow(rng().between(config.minFastWindow, config.maxFastWindow)),
      m_slowWindow(rng().between(config.minSlowWindow, config.maxSlowWindow))
{
}

void TrendAgent::onTick(const net::Tick& tick, OrderSink& orders)
{
    const std::int64_t price = referencePrice(tick) * kScale;
    if (m_samples == 0)
    {
        m_fast = price;
        m_slow = price;
    }
    else
    {
        // Exponential moving average: move 1/window of the way to the new price.
        m_fast += (price - m_fast) / m_fastWindow;
        m_slow += (price - m_slow) / m_slowWindow;
    }
    ++m_samples;

    // Last tick's unfilled remainder was chasing a price that has moved on.
    cancelOlderThan(0, orders);

    // No signal until the slow average has seen a full window of prices.
    if (m_samples < m_slowWindow || !rng().chance(m_config.actProbability))
    {
        return;
    }

    const Price lead = signal();
    if (lead > m_config.threshold && position() < m_config.positionLimit && tick.askPrice > 0)
    {
        orders.limit(Side::Buy, tick.askPrice + m_config.maxSlippage,
                     std::clamp<Quantity>(lead / m_config.threshold, 1, m_config.maxQuantity));
    }
    else if (lead < -m_config.threshold && position() > -m_config.positionLimit && tick.bidPrice > 0)
    {
        orders.limit(Side::Sell, std::max<Price>(tick.bidPrice - m_config.maxSlippage, 1),
                     std::clamp<Quantity>(-lead / m_config.threshold, 1, m_config.maxQuantity));
    }
}

} // namespace agora::agents
