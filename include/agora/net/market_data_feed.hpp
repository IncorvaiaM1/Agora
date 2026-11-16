/**
 * @file market_data_feed.hpp
 * @brief MarketDataFeed: broadcasts trades and tick snapshots to connected sessions.
 */
#pragma once

#include "agora/core/events.hpp"
#include "agora/net/session.hpp"

#include <vector>

namespace agora::net
{

/**
 * @brief Fan-out of public market data over the client connections.
 *
 * Keeps the list of registered sessions. A trade goes to every session that
 * subscribed to market data and to the two agents that took part in it; a
 * tick goes to everyone. Messages are only queued here; flush() writes each
 * session's batch with one send. Engine thread only.
 */
class MarketDataFeed
{
  public:
    /// Starts delivering to @p session. The session must outlive its registration.
    void add(Session& session);

    /// Stops delivering to @p session.
    void remove(Session& session);

    /// Queues one trade for its two parties and for all market data subscribers.
    void publishTrade(const TradeEvent& event);

    /// Queues the start-of-tick snapshot for every session.
    void publishTick(const Tick& tick);

    /// Queues the end-of-session notice for every session.
    void publishEnd(const End& end);

    /// Sends everything queued so far, one write per session.
    void flush();

    /// Price of the most recent trade, or 0 if nothing has traded yet.
    Price lastTradePrice() const noexcept { return m_lastTradePrice; }

    std::size_t sessionCount() const noexcept { return m_sessions.size(); }

  private:
    std::vector<Session*> m_sessions;
    Price m_lastTradePrice = 0;
};

} // namespace agora::net
