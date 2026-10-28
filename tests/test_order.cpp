// Unit tests for agora::Order.
#include "agora/core/order.hpp"

#include "doctest/doctest.h"

#include <stdexcept>
#include <string>

using agora::Order;
using agora::OrderType;
using agora::Side;

TEST_CASE("limit order construction")
{
    const Order order = Order::limit(1, 7, Side::Buy, 10050, 100);
    CHECK(order.id() == 1);
    CHECK(order.agent() == 7);
    CHECK(order.side() == Side::Buy);
    CHECK(order.type() == OrderType::Limit);
    CHECK(order.price() == 10050);
    CHECK(order.remainingQuantity() == 100);
    CHECK_FALSE(order.isFilled());
}

TEST_CASE("invalid orders are rejected")
{
    CHECK_THROWS_AS(Order::limit(1, 0, Side::Buy, 100, 0), std::invalid_argument);
    CHECK_THROWS_AS(Order::limit(1, 0, Side::Buy, 100, -5), std::invalid_argument);
    CHECK_THROWS_AS(Order::limit(1, 0, Side::Sell, 0, 10), std::invalid_argument);
    CHECK_THROWS_AS(Order::market(1, 0, Side::Sell, 0), std::invalid_argument);
}

TEST_CASE("partial and full fills")
{
    Order order = Order::limit(1, 0, Side::Sell, 10000, 100);
    order.fill(40);
    CHECK(order.filledQuantity() == 40);
    CHECK(order.remainingQuantity() == 60);
    order.fill(60);
    CHECK(order.isFilled());
}

TEST_CASE("overfill is rejected and leaves the order unchanged")
{
    Order order = Order::limit(1, 0, Side::Buy, 10000, 50);
    CHECK_THROWS_AS(order.fill(51), std::invalid_argument);
    CHECK_THROWS_AS(order.fill(0), std::invalid_argument);
    CHECK(order.remainingQuantity() == 50);
}

TEST_CASE("crossing rules")
{
    const Order buy = Order::limit(1, 0, Side::Buy, 10000, 10);
    CHECK(buy.crosses(9999));
    CHECK(buy.crosses(10000));
    CHECK_FALSE(buy.crosses(10001));

    const Order sell = Order::limit(2, 0, Side::Sell, 10000, 10);
    CHECK(sell.crosses(10001));
    CHECK(sell.crosses(10000));
    CHECK_FALSE(sell.crosses(9999));

    const Order market = Order::market(3, 0, Side::Buy, 10);
    CHECK(market.crosses(1));
    CHECK(market.crosses(1000000));
}

TEST_CASE("opposite side")
{
    CHECK(agora::opposite(Side::Buy) == Side::Sell);
    CHECK(agora::opposite(Side::Sell) == Side::Buy);
}

TEST_CASE("printable names")
{
    CHECK(std::string(agora::sideName(Side::Buy)) == "BUY");
    CHECK(std::string(agora::sideName(Side::Sell)) == "SELL");
    CHECK(std::string(agora::orderTypeName(OrderType::Limit)) == "LIMIT");
    CHECK(std::string(agora::orderTypeName(OrderType::Market)) == "MARKET");
}
