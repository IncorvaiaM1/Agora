/**
 * @file order_book.cpp
 * @brief Implementation of OrderBook.
 */
#include "agora/core/order_book.hpp"

#include <stdexcept>
#include <string>

namespace agora
{

void OrderBook::add(const Order& order)
{
    if (order.type() != OrderType::Limit)
    {
        throw std::logic_error("OrderBook::add: only limit orders can rest");
    }
    if (order.isFilled())
    {
        throw std::logic_error("OrderBook::add: order has no remaining quantity");
    }
    if (m_index.count(order.id()) != 0)
    {
        throw std::logic_error("OrderBook::add: duplicate order id " + std::to_string(order.id()));
    }

    std::list<Order>::iterator position;
    if (order.side() == Side::Buy)
    {
        if (!m_asks.empty() && order.price() >= m_asks.begin()->first)
        {
            throw std::logic_error("OrderBook::add: bid would cross the book");
        }
        Level& level = m_bids[order.price()];
        position = level.orders.insert(level.orders.end(), order);
        level.total += order.remainingQuantity();
    }
    else
    {
        if (!m_bids.empty() && order.price() <= m_bids.begin()->first)
        {
            throw std::logic_error("OrderBook::add: ask would cross the book");
        }
        Level& level = m_asks[order.price()];
        position = level.orders.insert(level.orders.end(), order);
        level.total += order.remainingQuantity();
    }
    m_index.emplace(order.id(), Location{order.side(), order.price(), position});
}

std::optional<Order> OrderBook::cancel(OrderId id)
{
    const auto found = m_index.find(id);
    if (found == m_index.end())
    {
        return std::nullopt;
    }
    const Location location = found->second;
    m_index.erase(found);

    // Removes the order from its level and drops the level if it is now empty.
    auto removeFrom = [&location](auto& levels)
    {
        auto levelIt = levels.find(location.price);
        Level& level = levelIt->second;
        Order removed = *location.position;
        level.total -= removed.remainingQuantity();
        level.orders.erase(location.position);
        if (level.orders.empty())
        {
            levels.erase(levelIt);
        }
        return removed;
    };

    return location.side == Side::Buy ? removeFrom(m_bids) : removeFrom(m_asks);
}

template <typename Levels>
void OrderBook::matchAgainst(Levels& levels, Order& taker, std::vector<Fill>& fills)
{
    while (!taker.isFilled() && !levels.empty())
    {
        auto levelIt = levels.begin();
        const Price levelPrice = levelIt->first;
        if (!taker.crosses(levelPrice))
        {
            break;
        }

        Level& level = levelIt->second;
        while (!taker.isFilled() && !level.orders.empty())
        {
            Order& maker = level.orders.front();
            const Quantity quantity = maker.remainingQuantity() < taker.remainingQuantity()
                                          ? maker.remainingQuantity()
                                          : taker.remainingQuantity();
            maker.fill(quantity);
            taker.fill(quantity);
            level.total -= quantity;
            fills.push_back(Fill{maker.id(), maker.agent(), levelPrice, quantity});

            if (maker.isFilled())
            {
                m_index.erase(maker.id());
                level.orders.pop_front();
            }
        }

        if (level.orders.empty())
        {
            levels.erase(levelIt);
        }
    }
}

void OrderBook::match(Order& taker, std::vector<Fill>& fills)
{
    if (taker.side() == Side::Buy)
    {
        matchAgainst(m_asks, taker, fills);
    }
    else
    {
        matchAgainst(m_bids, taker, fills);
    }
}

const Order* OrderBook::find(OrderId id) const
{
    const auto found = m_index.find(id);
    return found == m_index.end() ? nullptr : &*found->second.position;
}

std::optional<Price> OrderBook::bestBid() const
{
    if (m_bids.empty())
    {
        return std::nullopt;
    }
    return m_bids.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const
{
    if (m_asks.empty())
    {
        return std::nullopt;
    }
    return m_asks.begin()->first;
}

Quantity OrderBook::volumeAt(Side side, Price price) const
{
    if (side == Side::Buy)
    {
        const auto it = m_bids.find(price);
        return it == m_bids.end() ? 0 : it->second.total;
    }
    const auto it = m_asks.find(price);
    return it == m_asks.end() ? 0 : it->second.total;
}

template <typename Levels>
void OrderBook::appendDepth(const Levels& levels, std::size_t maxLevels, std::vector<LevelSummary>& out)
{
    for (auto it = levels.begin(); it != levels.end() && out.size() < maxLevels; ++it)
    {
        out.push_back(LevelSummary{it->first, it->second.total, it->second.orders.size()});
    }
}

std::vector<LevelSummary> OrderBook::depth(Side side, std::size_t maxLevels) const
{
    std::vector<LevelSummary> out;
    if (side == Side::Buy)
    {
        appendDepth(m_bids, maxLevels, out);
    }
    else
    {
        appendDepth(m_asks, maxLevels, out);
    }
    return out;
}

std::size_t OrderBook::levelCount(Side side) const noexcept
{
    return side == Side::Buy ? m_bids.size() : m_asks.size();
}

template <typename Levels>
void OrderBook::checkSide(const Levels& levels, Side side, std::size_t& indexedSeen) const
{
    const std::string sideLabel = sideName(side);
    for (const auto& [price, level] : levels)
    {
        if (level.orders.empty())
        {
            throw std::logic_error(sideLabel + " level " + std::to_string(price) + " is empty");
        }
        Quantity sum = 0;
        SequenceNumber previousSequence = 0;
        bool first = true;
        for (auto it = level.orders.begin(); it != level.orders.end(); ++it)
        {
            const Order& order = *it;
            if (order.side() != side || order.price() != price)
            {
                throw std::logic_error("order " + std::to_string(order.id()) + " is on the wrong level");
            }
            if (order.isFilled())
            {
                throw std::logic_error("filled order " + std::to_string(order.id()) + " still resting");
            }
            if (!first && order.sequence() <= previousSequence)
            {
                throw std::logic_error(sideLabel + " level " + std::to_string(price) +
                                       " is not in time priority");
            }
            const auto indexed = m_index.find(order.id());
            if (indexed == m_index.end() || indexed->second.position != it)
            {
                throw std::logic_error("order " + std::to_string(order.id()) + " missing from index");
            }
            sum += order.remainingQuantity();
            previousSequence = order.sequence();
            first = false;
            ++indexedSeen;
        }
        if (sum != level.total)
        {
            throw std::logic_error(sideLabel + " level " + std::to_string(price) + " total is stale");
        }
    }
}

void OrderBook::checkInvariants() const
{
    if (!m_bids.empty() && !m_asks.empty() && m_bids.begin()->first >= m_asks.begin()->first)
    {
        throw std::logic_error("book is crossed: best bid " + std::to_string(m_bids.begin()->first) +
                               " >= best ask " + std::to_string(m_asks.begin()->first));
    }
    std::size_t indexedSeen = 0;
    checkSide(m_bids, Side::Buy, indexedSeen);
    checkSide(m_asks, Side::Sell, indexedSeen);
    if (indexedSeen != m_index.size())
    {
        throw std::logic_error("index holds orders that are not on the book");
    }
}

} // namespace agora
