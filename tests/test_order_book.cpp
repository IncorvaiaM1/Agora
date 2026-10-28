// Unit tests for agora::OrderBook.
#include "agora/core/order_book.hpp"

#include "doctest/doctest.h"

#include <stdexcept>
#include <vector>

using agora::Fill;
using agora::Order;
using agora::OrderBook;
using agora::Side;

namespace
{

/// Builds a resting limit order with sequence == id, so time priority follows id.
Order resting(agora::OrderId id, Side side, agora::Price price, agora::Quantity quantity,
              agora::AgentId agent = 1)
{
    Order order = Order::limit(id, agent, side, price, quantity);
    order.setSequence(id);
    return order;
}

} // namespace

TEST_CASE("empty book")
{
    const OrderBook book;
    CHECK(book.empty());
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.bestAsk().has_value());
    CHECK(book.find(1) == nullptr);
    CHECK_NOTHROW(book.checkInvariants());
}

TEST_CASE("best prices and depth")
{
    OrderBook book;
    book.add(resting(1, Side::Buy, 99, 10));
    book.add(resting(2, Side::Buy, 100, 5));
    book.add(resting(3, Side::Buy, 100, 7));
    book.add(resting(4, Side::Sell, 102, 3));
    book.add(resting(5, Side::Sell, 101, 4));

    CHECK(*book.bestBid() == 100);
    CHECK(*book.bestAsk() == 101);
    CHECK(book.volumeAt(Side::Buy, 100) == 12);
    CHECK(book.volumeAt(Side::Buy, 98) == 0);
    CHECK(book.levelCount(Side::Buy) == 2);
    CHECK(book.orderCount() == 5);

    const auto bids = book.depth(Side::Buy, 10);
    REQUIRE(bids.size() == 2);
    CHECK(bids[0].price == 100);
    CHECK(bids[0].quantity == 12);
    CHECK(bids[0].orderCount == 2);
    CHECK(bids[1].price == 99);

    const auto asks = book.depth(Side::Sell, 1);
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].price == 101);
    CHECK_NOTHROW(book.checkInvariants());
}

TEST_CASE("add rejects bad input")
{
    OrderBook book;
    book.add(resting(1, Side::Sell, 101, 10));
    CHECK_THROWS_AS(book.add(resting(1, Side::Buy, 90, 1)), std::logic_error);  // duplicate id
    CHECK_THROWS_AS(book.add(resting(2, Side::Buy, 101, 1)), std::logic_error); // would cross
    CHECK_THROWS_AS(book.add(Order::market(3, 1, Side::Buy, 1)), std::logic_error);
    CHECK(book.orderCount() == 1);
}

TEST_CASE("cancel removes the order and empty levels")
{
    OrderBook book;
    book.add(resting(1, Side::Buy, 100, 5));
    book.add(resting(2, Side::Buy, 100, 6));

    const auto removed = book.cancel(1);
    REQUIRE(removed.has_value());
    CHECK(removed->id() == 1);
    CHECK(book.volumeAt(Side::Buy, 100) == 6);
    CHECK_FALSE(book.cancel(1).has_value());

    book.cancel(2);
    CHECK(book.empty());
    CHECK(book.levelCount(Side::Buy) == 0);
    CHECK_NOTHROW(book.checkInvariants());
}

TEST_CASE("match honors time priority within a level")
{
    OrderBook book;
    book.add(resting(1, Side::Sell, 100, 5, 11));
    book.add(resting(2, Side::Sell, 100, 5, 12));

    Order taker = Order::limit(10, 99, Side::Buy, 100, 7);
    std::vector<Fill> fills;
    book.match(taker, fills);

    REQUIRE(fills.size() == 2);
    CHECK(fills[0].makerOrderId == 1);
    CHECK(fills[0].makerAgent == 11);
    CHECK(fills[0].quantity == 5);
    CHECK(fills[1].makerOrderId == 2);
    CHECK(fills[1].quantity == 2);
    CHECK(taker.isFilled());
    CHECK(book.find(1) == nullptr);
    CHECK(book.find(2)->remainingQuantity() == 3);
    CHECK(book.volumeAt(Side::Sell, 100) == 3);
    CHECK_NOTHROW(book.checkInvariants());
}

TEST_CASE("match sweeps levels best price first and stops at the limit")
{
    OrderBook book;
    book.add(resting(1, Side::Buy, 100, 4));
    book.add(resting(2, Side::Buy, 99, 4));
    book.add(resting(3, Side::Buy, 98, 4));

    Order taker = Order::limit(10, 2, Side::Sell, 99, 20);
    std::vector<Fill> fills;
    book.match(taker, fills);

    REQUIRE(fills.size() == 2);
    CHECK(fills[0].price == 100); // executes at the maker's price
    CHECK(fills[1].price == 99);
    CHECK(taker.remainingQuantity() == 12);
    CHECK(*book.bestBid() == 98);
    CHECK_NOTHROW(book.checkInvariants());
}

TEST_CASE("market order consumes until the side is empty")
{
    OrderBook book;
    book.add(resting(1, Side::Sell, 101, 3));
    book.add(resting(2, Side::Sell, 105, 3));

    Order taker = Order::market(10, 2, Side::Buy, 10);
    std::vector<Fill> fills;
    book.match(taker, fills);

    CHECK(fills.size() == 2);
    CHECK(taker.remainingQuantity() == 4);
    CHECK(book.empty());
}

TEST_CASE("non-crossing taker does nothing")
{
    OrderBook book;
    book.add(resting(1, Side::Sell, 101, 3));
    Order taker = Order::limit(10, 2, Side::Buy, 100, 5);
    std::vector<Fill> fills;
    book.match(taker, fills);
    CHECK(fills.empty());
    CHECK(taker.remainingQuantity() == 5);
    CHECK(book.orderCount() == 1);
}
