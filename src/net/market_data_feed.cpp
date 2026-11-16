/**
 * @file market_data_feed.cpp
 * @brief Implementation of MarketDataFeed.
 */
#include "agora/net/market_data_feed.hpp"

#include <algorithm>

namespace agora::net
{

void MarketDataFeed::add(Session& session)
{
    m_sessions.push_back(&session);
}

void MarketDataFeed::remove(Session& session)
{
    m_sessions.erase(std::remove(m_sessions.begin(), m_sessions.end(), &session), m_sessions.end());
}

void MarketDataFeed::publishTrade(const TradeEvent& event)
{
    m_lastTradePrice = event.price;

    Trade trade;
    trade.sequence = event.sequence;
    trade.tradeId = event.tradeId;
    trade.buyOrderId = event.buyOrderId;
    trade.sellOrderId = event.sellOrderId;
    trade.buyAgent = event.buyAgent;
    trade.sellAgent = event.sellAgent;
    trade.price = event.price;
    trade.quantity = event.quantity;
    trade.aggressor = event.aggressor;

    for (Session* session : m_sessions)
    {
        const bool isParty =
            !session->isObserver() && (session->agent() == event.buyAgent || session->agent() == event.sellAgent);
        if (isParty || session->wantsMarketData())
        {
            session->send(trade);
        }
    }
}

void MarketDataFeed::publishTick(const Tick& tick)
{
    for (Session* session : m_sessions)
    {
        session->send(tick);
    }
}

void MarketDataFeed::publishEnd(const End& end)
{
    for (Session* session : m_sessions)
    {
        session->send(end);
    }
}

void MarketDataFeed::flush()
{
    for (Session* session : m_sessions)
    {
        session->flush();
    }
}

} // namespace agora::net
