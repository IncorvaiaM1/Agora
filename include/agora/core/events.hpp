/**
 * @file events.hpp
 * @brief Events emitted by the matching engine and the listener interface.
 *
 * The engine reports everything it does through an EventListener. The trade
 * log writer, the market data feed and the per-client sessions are all
 * listeners, so the engine itself never performs I/O.
 */
#pragma once

#include "agora/core/types.hpp"

#include <vector>

namespace agora
{

/// Why a command was refused.
enum class RejectReason : std::uint8_t
{
    InvalidQuantity = 1, ///< Quantity was zero or negative.
    InvalidPrice = 2,    ///< Limit price was zero or negative.
    UnknownOrder = 3,    ///< Cancel target is not resting on the book.
    NotOwner = 4,        ///< Cancel target belongs to a different agent.
    InvalidCommand = 5   ///< Unrecognized command type or order type.
};

/// Why an order left the book without trading.
enum class CancelReason : std::uint8_t
{
    Requested = 1,  ///< The owning agent asked for it.
    NoLiquidity = 2 ///< Unfilled remainder of a market order (market orders never rest).
};

/// Printable name of a reject reason.
const char* rejectReasonName(RejectReason reason) noexcept;

/// Printable name of a cancel reason.
const char* cancelReasonName(CancelReason reason) noexcept;

/// A new order passed validation and was assigned an exchange OrderId.
struct AcceptedEvent
{
    SequenceNumber sequence;
    OrderId orderId;
    AgentId agent;
    ClientOrderId clientOrderId;
    Side side;
    OrderType orderType;
    Price price;
    Quantity quantity;
};

/// A command failed validation; nothing changed on the book.
struct RejectedEvent
{
    SequenceNumber sequence;
    AgentId agent;
    ClientOrderId clientOrderId;
    RejectReason reason;
};

/// Quantity removed from the book (or from an incoming market order) without trading.
struct CancelledEvent
{
    SequenceNumber sequence;
    OrderId orderId;
    AgentId agent;
    Quantity cancelledQuantity;
    CancelReason reason;
};

/// One execution between a resting (maker) order and an incoming (taker) order.
struct TradeEvent
{
    SequenceNumber sequence; ///< Sequence of the command that caused the trade.
    TradeId tradeId;
    OrderId buyOrderId;
    OrderId sellOrderId;
    AgentId buyAgent;
    AgentId sellAgent;
    Price price; ///< Always the maker's resting price.
    Quantity quantity;
    Side aggressor; ///< Side of the taker order.
};

/**
 * @brief Receives engine events. All callbacks default to doing nothing,
 * so a listener only overrides what it needs.
 *
 * Callbacks run synchronously on the engine thread, in sequence order.
 */
class EventListener
{
  public:
    virtual ~EventListener() = default;

    virtual void onAccepted(const AcceptedEvent& /*event*/) {}
    virtual void onRejected(const RejectedEvent& /*event*/) {}
    virtual void onCancelled(const CancelledEvent& /*event*/) {}
    virtual void onTrade(const TradeEvent& /*event*/) {}
};

/// Listener that discards everything (used by benchmarks).
class NullListener final : public EventListener
{
};

/**
 * @brief Forwards every event to several listeners, in registration order.
 *
 * Listeners are held by pointer and must outlive the fan-out.
 */
class ListenerFanout final : public EventListener
{
  public:
    void add(EventListener& listener) { m_listeners.push_back(&listener); }

    void onAccepted(const AcceptedEvent& event) override
    {
        for (EventListener* listener : m_listeners)
        {
            listener->onAccepted(event);
        }
    }
    void onRejected(const RejectedEvent& event) override
    {
        for (EventListener* listener : m_listeners)
        {
            listener->onRejected(event);
        }
    }
    void onCancelled(const CancelledEvent& event) override
    {
        for (EventListener* listener : m_listeners)
        {
            listener->onCancelled(event);
        }
    }
    void onTrade(const TradeEvent& event) override
    {
        for (EventListener* listener : m_listeners)
        {
            listener->onTrade(event);
        }
    }

  private:
    std::vector<EventListener*> m_listeners;
};

} // namespace agora
