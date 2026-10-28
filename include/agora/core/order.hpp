/**
 * @file order.hpp
 * @brief The Order class: a single instruction to buy or sell.
 */
#pragma once

#include "agora/core/types.hpp"

namespace agora
{

/**
 * @brief A limit or market order and its fill state.
 *
 * An Order is created by the gateway, sequenced by the matching engine and,
 * if it is a limit order with quantity left after matching, rests in the
 * OrderBook. The class enforces its own invariants: quantities are positive,
 * filled quantity never exceeds original quantity, and market orders carry
 * no limit price.
 */
class Order
{
  public:
    /**
     * @brief Constructs a limit order.
     * @throws std::invalid_argument if quantity <= 0 or price <= 0.
     */
    static Order limit(OrderId id, AgentId agent, Side side, Price price, Quantity quantity);

    /**
     * @brief Constructs a market order.
     * @throws std::invalid_argument if quantity <= 0.
     */
    static Order market(OrderId id, AgentId agent, Side side, Quantity quantity);

    OrderId id() const noexcept { return m_id; }
    AgentId agent() const noexcept { return m_agent; }
    Side side() const noexcept { return m_side; }
    OrderType type() const noexcept { return m_type; }

    /// Limit price in ticks. Zero for market orders.
    Price price() const noexcept { return m_price; }

    Quantity originalQuantity() const noexcept { return m_originalQuantity; }
    Quantity filledQuantity() const noexcept { return m_filledQuantity; }
    Quantity remainingQuantity() const noexcept { return m_originalQuantity - m_filledQuantity; }
    bool isFilled() const noexcept { return remainingQuantity() == 0; }

    /// Engine sequence number; defines time priority within a price level.
    SequenceNumber sequence() const noexcept { return m_sequence; }
    void setSequence(SequenceNumber sequence) noexcept { m_sequence = sequence; }

    /**
     * @brief Records a (partial) fill against this order.
     * @param quantity Shares filled; must satisfy 0 < quantity <= remainingQuantity().
     * @throws std::invalid_argument if the fill would violate that bound.
     */
    void fill(Quantity quantity);

    /**
     * @brief True if this order is willing to trade at the given price.
     *
     * Market orders cross any price. A buy limit crosses at or below its
     * limit; a sell limit crosses at or above its limit.
     */
    bool crosses(Price price) const noexcept;

  private:
    Order(OrderId id, AgentId agent, Side side, OrderType type, Price price, Quantity quantity);

    OrderId m_id;
    AgentId m_agent;
    Side m_side;
    OrderType m_type;
    Price m_price;
    Quantity m_originalQuantity;
    Quantity m_filledQuantity = 0;
    SequenceNumber m_sequence = 0;
};

} // namespace agora
