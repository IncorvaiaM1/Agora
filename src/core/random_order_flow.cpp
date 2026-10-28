/**
 * @file random_order_flow.cpp
 * @brief Implementation of RandomOrderFlow.
 */
#include "agora/core/random_order_flow.hpp"

namespace agora
{

RandomOrderFlow::RandomOrderFlow(const FlowConfig& config)
    : m_config(config), m_rng(config.seed), m_mid(config.startMid)
{
}

Command RandomOrderFlow::next()
{
    // Random walk of the reference price so the book keeps moving.
    if (m_rng.chance(m_config.driftProbability))
    {
        m_mid += m_rng.chance(0.5) ? 1 : -1;
        if (m_mid <= m_config.maxOffset)
        {
            m_mid = m_config.maxOffset + 1;
        }
    }

    const ClientOrderId clientId = m_nextClientId++;

    if (m_rng.chance(m_config.invalidProbability))
    {
        // Zero quantity: always rejected by the engine.
        return Command::newLimit(1, clientId, Side::Buy, m_mid, 0);
    }

    if (!m_live.empty() && m_rng.chance(m_config.cancelProbability))
    {
        // Draw random candidates (swap-and-pop is O(1)) until one is still resting.
        while (!m_cancelCandidates.empty())
        {
            const auto index = static_cast<std::size_t>(m_rng.below(m_cancelCandidates.size()));
            const OrderId orderId = m_cancelCandidates[index];
            m_cancelCandidates[index] = m_cancelCandidates.back();
            m_cancelCandidates.pop_back();
            const auto live = m_live.find(orderId);
            if (live != m_live.end())
            {
                return Command::cancel(live->second.first, clientId, orderId);
            }
        }
    }

    const auto agent = static_cast<AgentId>(m_rng.between(1, m_config.agents));
    const Side side = m_rng.chance(0.5) ? Side::Buy : Side::Sell;
    const Quantity quantity = m_rng.between(1, m_config.maxQuantity);

    if (m_rng.chance(m_config.marketProbability))
    {
        return Command::newMarket(agent, clientId, side, quantity);
    }
    // Passive orders rest on their own side of mid (bids below, asks above) and
    // build depth; aggressive ones are priced through mid and usually trade.
    const Price distance = m_rng.between(1, m_config.maxOffset);
    const bool aggressive = m_rng.chance(m_config.aggressiveProbability);
    const bool below = (side == Side::Buy) != aggressive;
    const Price price = below ? m_mid - distance : m_mid + distance;
    return Command::newLimit(agent, clientId, side, price, quantity);
}

void RandomOrderFlow::onAccepted(const AcceptedEvent& event)
{
    if (event.orderType == OrderType::Limit)
    {
        m_live.emplace(event.orderId, std::make_pair(event.agent, event.quantity));
        m_cancelCandidates.push_back(event.orderId);
    }
}

void RandomOrderFlow::onTrade(const TradeEvent& event)
{
    reduce(event.buyOrderId, event.quantity);
    reduce(event.sellOrderId, event.quantity);
}

void RandomOrderFlow::onCancelled(const CancelledEvent& event)
{
    m_live.erase(event.orderId);
}

void RandomOrderFlow::reduce(OrderId id, Quantity quantity)
{
    const auto live = m_live.find(id);
    if (live == m_live.end())
    {
        return; // market order or already gone
    }
    live->second.second -= quantity;
    if (live->second.second <= 0)
    {
        m_live.erase(live);
    }
}

} // namespace agora
