/**
 * @file order.cpp
 * @brief Implementation of Order and the printable names in types.hpp.
 */
#include "agora/core/order.hpp"

#include <stdexcept>

namespace agora
{

const char* sideName(Side side) noexcept
{
    return side == Side::Buy ? "BUY" : "SELL";
}

const char* orderTypeName(OrderType type) noexcept
{
    return type == OrderType::Limit ? "LIMIT" : "MARKET";
}

Order::Order(OrderId id, AgentId agent, Side side, OrderType type, Price price, Quantity quantity)
    : m_id(id), m_agent(agent), m_side(side), m_type(type), m_price(price), m_originalQuantity(quantity)
{
    if (quantity <= 0)
    {
        throw std::invalid_argument("Order quantity must be positive");
    }
}

Order Order::limit(OrderId id, AgentId agent, Side side, Price price, Quantity quantity)
{
    if (price <= 0)
    {
        throw std::invalid_argument("Limit price must be positive");
    }
    return Order(id, agent, side, OrderType::Limit, price, quantity);
}

Order Order::market(OrderId id, AgentId agent, Side side, Quantity quantity)
{
    return Order(id, agent, side, OrderType::Market, 0, quantity);
}

void Order::fill(Quantity quantity)
{
    if (quantity <= 0 || quantity > remainingQuantity())
    {
        throw std::invalid_argument("Fill quantity out of range");
    }
    m_filledQuantity += quantity;
}

bool Order::crosses(Price price) const noexcept
{
    if (m_type == OrderType::Market)
    {
        return true;
    }
    return m_side == Side::Buy ? price <= m_price : price >= m_price;
}

} // namespace agora
