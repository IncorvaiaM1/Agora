/**
 * @file market_maker_agent.cpp
 * @brief Implementation of MarketMakerAgent.
 */
#include "agora/agents/market_maker_agent.hpp"

#include <algorithm>

namespace agora::agents
{

MarketMakerAgent::MarketMakerAgent(AgentId id, std::uint64_t seed, MarketMakerConfig config)
    : Agent(id, seed), m_config(config)
{
}

Price MarketMakerAgent::centre(const net::Tick& tick) const noexcept
{
    // The last trade, not the mid (the mid is partly this agent's own quotes),
    // averaged with the fundamental so the quotes stay tied to value.
    const Price anchor =
        tick.lastTradePrice > 0 ? (tick.lastTradePrice + tick.fundamental) / 2 : tick.fundamental;
    const Price skew = std::clamp<Price>(position() / m_config.sharesPerSkewTick, -m_config.maxSkew, m_config.maxSkew);
    return anchor - skew;
}

void MarketMakerAgent::onTick(const net::Tick& tick, OrderSink& orders)
{
    const Price middle = centre(tick);
    const Price bidPrice = std::max<Price>(middle - m_config.halfSpread, 1);
    const Price askPrice = std::max<Price>(middle + m_config.halfSpread, bidPrice + 1);
    const bool wantBid = position() < m_config.positionLimit;
    const bool wantAsk = position() > -m_config.positionLimit;

    // Forget cancel requests for orders that are gone (cancelled or filled meanwhile).
    for (auto it = m_cancelling.begin(); it != m_cancelling.end();)
    {
        it = openOrders().count(*it) == 0 ? m_cancelling.erase(it) : std::next(it);
    }

    // Pull every quote that is no longer where it should be.
    bool haveBid = m_pendingBid != 0;
    bool haveAsk = m_pendingAsk != 0;
    for (const auto& entry : openOrders())
    {
        const OpenOrder& quote = entry.second;
        const bool isBid = quote.side == Side::Buy;
        const Price drift = quote.price - (isBid ? bidPrice : askPrice);
        const bool inPlace = drift >= -m_config.requoteTolerance && drift <= m_config.requoteTolerance;
        const bool keep = inPlace && (isBid ? (wantBid && !haveBid) : (wantAsk && !haveAsk));
        if (keep)
        {
            (isBid ? haveBid : haveAsk) = true;
        }
        else if (m_cancelling.insert(entry.first).second)
        {
            orders.cancel(entry.first);
        }
    }

    // Replace whatever is missing.
    if (wantBid && !haveBid)
    {
        m_pendingBid = orders.limit(Side::Buy, bidPrice, m_config.quoteSize);
    }
    if (wantAsk && !haveAsk)
    {
        m_pendingAsk = orders.limit(Side::Sell, askPrice, m_config.quoteSize);
    }
}

void MarketMakerAgent::onAccepted(const net::Accepted& accepted)
{
    if (accepted.clientOrderId == m_pendingBid)
    {
        m_pendingBid = 0;
    }
    else if (accepted.clientOrderId == m_pendingAsk)
    {
        m_pendingAsk = 0;
    }
}

void MarketMakerAgent::onRejected(const net::Rejected& rejected)
{
    if (rejected.clientOrderId == m_pendingBid)
    {
        m_pendingBid = 0;
    }
    else if (rejected.clientOrderId == m_pendingAsk)
    {
        m_pendingAsk = 0;
    }
}

void MarketMakerAgent::onCancelled(const net::Cancelled& cancelled)
{
    m_cancelling.erase(cancelled.orderId);
}

} // namespace agora::agents
