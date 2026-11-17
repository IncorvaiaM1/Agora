/**
 * @file zero_intelligence_agent.cpp
 * @brief Implementation of ZeroIntelligenceAgent.
 */
#include "agora/agents/zero_intelligence_agent.hpp"

#include <algorithm>

namespace agora::agents
{

ZeroIntelligenceAgent::ZeroIntelligenceAgent(AgentId id, std::uint64_t seed, ZeroIntelligenceConfig config)
    : Agent(id, seed), m_config(config)
{
}

void ZeroIntelligenceAgent::onTick(const net::Tick& tick, OrderSink& orders)
{
    if (!rng().chance(m_config.actProbability))
    {
        return;
    }

    const bool tooManyOpen = openOrders().size() > m_config.maxOpenOrders;
    const double action = rng().uniform();

    if (!openOrders().empty() && (tooManyOpen || action < m_config.cancelProbability))
    {
        // Oldest order first: order ids increase over time and the map is sorted.
        orders.cancel(openOrders().begin()->first);
        return;
    }

    const Side side = rng().chance(0.5) ? Side::Buy : Side::Sell;
    const Quantity quantity = rng().between(1, m_config.maxQuantity);

    if (action < m_config.cancelProbability + m_config.marketProbability)
    {
        orders.market(side, quantity);
        return;
    }

    // Uniform price in a window that sits mostly behind the reference price
    // (passive, rests on the book) and partly through it (aggressive, trades).
    const Price reference = referencePrice(tick);
    const Price offset = rng().between(-m_config.aggression, m_config.depth);
    const Price price = side == Side::Buy ? reference - offset : reference + offset;
    orders.limit(side, std::max<Price>(price, 1), quantity);
}

} // namespace agora::agents
