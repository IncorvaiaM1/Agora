/**
 * @file order_book.hpp
 * @brief OrderBook: resting limit orders with price-time priority.
 */
#pragma once

#include "agora/core/order.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace agora
{

/// Aggregate view of one price level, used for market data and the viewer.
struct LevelSummary
{
    Price price;
    Quantity quantity;      ///< Total remaining quantity at this price.
    std::size_t orderCount; ///< Number of resting orders at this price.
};

/// One execution against a resting order, produced by OrderBook::match().
struct Fill
{
    OrderId makerOrderId;
    AgentId makerAgent;
    Price price;
    Quantity quantity;
};

/**
 * @brief The limit order book for a single instrument.
 *
 * Bids and asks are kept in ordered maps of price levels (best price first).
 * Each level is a FIFO list of orders, which gives time priority within a
 * price. A hash index from OrderId to the order's position makes cancels O(1)
 * after the level lookup.
 *
 * The book only stores and consumes liquidity. Validation, id assignment and
 * event reporting belong to the MatchingEngine.
 */
class OrderBook
{
  public:
    /**
     * @brief Rests a limit order on its side of the book.
     * @pre The order is a limit order with remaining quantity, its id is not
     *      already on the book, and it does not cross the opposite best price.
     * @throws std::logic_error if a precondition is violated.
     */
    void add(const Order& order);

    /**
     * @brief Removes a resting order.
     * @return The removed order, or std::nullopt if no such order is resting.
     */
    std::optional<Order> cancel(OrderId id);

    /**
     * @brief Matches an incoming order against the opposite side.
     *
     * Consumes resting liquidity best price first, oldest order first, while
     * the taker crosses and has quantity left. The taker's fill state is
     * updated in place and one Fill per execution is appended to @p fills.
     * Fully filled makers are removed from the book.
     */
    void match(Order& taker, std::vector<Fill>& fills);

    /// Pointer to a resting order, or nullptr if it is not on the book.
    const Order* find(OrderId id) const;

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    /// Remaining quantity resting at @p price on @p side (0 if no level).
    Quantity volumeAt(Side side, Price price) const;

    /// Up to @p maxLevels levels of @p side, best price first.
    std::vector<LevelSummary> depth(Side side, std::size_t maxLevels) const;

    std::size_t orderCount() const noexcept { return m_index.size(); }
    std::size_t levelCount(Side side) const noexcept;
    bool empty() const noexcept { return m_index.empty(); }

    /**
     * @brief Verifies every structural invariant of the book.
     *
     * Checks that the book is not crossed, no level is empty, each level's
     * cached total equals the sum of its orders, orders sit on the right side
     * at the right price in sequence order, and the index matches the levels.
     *
     * @throws std::logic_error describing the first violation found.
     */
    void checkInvariants() const;

  private:
    /// All orders resting at one price, oldest first.
    struct Level
    {
        Quantity total = 0;
        std::list<Order> orders;
    };

    using BidLevels = std::map<Price, Level, std::greater<Price>>;
    using AskLevels = std::map<Price, Level, std::less<Price>>;

    /// Where a resting order lives, so cancel() does not have to search.
    struct Location
    {
        Side side;
        Price price;
        std::list<Order>::iterator position;
    };

    template <typename Levels> void matchAgainst(Levels& levels, Order& taker, std::vector<Fill>& fills);

    template <typename Levels>
    static void appendDepth(const Levels& levels, std::size_t maxLevels, std::vector<LevelSummary>& out);

    template <typename Levels>
    void checkSide(const Levels& levels, Side side, std::size_t& indexedSeen) const;

    BidLevels m_bids;
    AskLevels m_asks;
    std::unordered_map<OrderId, Location> m_index;
};

} // namespace agora
