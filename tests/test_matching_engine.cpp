// Unit tests for agora::MatchingEngine.
#include "agora/core/matching_engine.hpp"
#include "recording_listener.hpp"

#include "doctest/doctest.h"

using agora::CancelReason;
using agora::Command;
using agora::MatchingEngine;
using agora::RejectReason;
using agora::Side;
using agora::test::RecordingListener;

namespace
{

agora::EngineConfig checked()
{
    agora::EngineConfig config;
    config.checkInvariantsEveryCommand = true;
    return config;
}

} // namespace

TEST_CASE("resting limit order is accepted with an id and sequence")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());

    const auto sequence = engine.process(Command::newLimit(7, 1001, Side::Buy, 100, 10));
    CHECK(sequence == 1);
    REQUIRE(events.accepted.size() == 1);
    CHECK(events.accepted[0].orderId == 1);
    CHECK(events.accepted[0].agent == 7);
    CHECK(events.accepted[0].clientOrderId == 1001);
    CHECK(events.trades.empty());
    CHECK(*engine.book().bestBid() == 100);
}

TEST_CASE("crossing limit order trades at the maker price and rests the remainder")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());

    engine.process(Command::newLimit(1, 1, Side::Sell, 101, 5)); // order 1
    engine.process(Command::newLimit(2, 1, Side::Buy, 103, 8));  // order 2, crosses

    REQUIRE(events.trades.size() == 1);
    const auto& trade = events.trades[0];
    CHECK(trade.sequence == 2);
    CHECK(trade.tradeId == 1);
    CHECK(trade.buyOrderId == 2);
    CHECK(trade.sellOrderId == 1);
    CHECK(trade.buyAgent == 2);
    CHECK(trade.sellAgent == 1);
    CHECK(trade.price == 101);
    CHECK(trade.quantity == 5);
    CHECK(trade.aggressor == Side::Buy);

    // Remaining 3 rests at the taker's own limit.
    CHECK(*engine.book().bestBid() == 103);
    CHECK(engine.book().volumeAt(Side::Buy, 103) == 3);
    CHECK_FALSE(engine.book().bestAsk().has_value());
}

TEST_CASE("sell aggressor fields are mirrored")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());
    engine.process(Command::newLimit(1, 1, Side::Buy, 100, 5));
    engine.process(Command::newLimit(2, 1, Side::Sell, 99, 5));

    REQUIRE(events.trades.size() == 1);
    CHECK(events.trades[0].buyOrderId == 1);
    CHECK(events.trades[0].sellOrderId == 2);
    CHECK(events.trades[0].price == 100);
    CHECK(events.trades[0].aggressor == Side::Sell);
    CHECK(engine.book().empty());
}

TEST_CASE("market order remainder is cancelled for lack of liquidity")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());
    engine.process(Command::newLimit(1, 1, Side::Sell, 101, 4));
    engine.process(Command::newMarket(2, 1, Side::Buy, 10));

    REQUIRE(events.trades.size() == 1);
    CHECK(events.trades[0].quantity == 4);
    REQUIRE(events.cancelled.size() == 1);
    CHECK(events.cancelled[0].orderId == 2);
    CHECK(events.cancelled[0].cancelledQuantity == 6);
    CHECK(events.cancelled[0].reason == CancelReason::NoLiquidity);
    CHECK(engine.book().empty());
}

TEST_CASE("market order into an empty book")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());
    engine.process(Command::newMarket(1, 1, Side::Sell, 10));
    CHECK(events.accepted.size() == 1);
    CHECK(events.trades.empty());
    REQUIRE(events.cancelled.size() == 1);
    CHECK(events.cancelled[0].cancelledQuantity == 10);
}

TEST_CASE("cancel by owner, by stranger, and of unknown order")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());
    engine.process(Command::newLimit(5, 1, Side::Buy, 100, 10)); // order 1

    engine.process(Command::cancel(6, 2, 1));
    REQUIRE(events.rejected.size() == 1);
    CHECK(events.rejected[0].reason == RejectReason::NotOwner);
    CHECK(engine.book().orderCount() == 1);

    engine.process(Command::cancel(5, 3, 1));
    REQUIRE(events.cancelled.size() == 1);
    CHECK(events.cancelled[0].reason == CancelReason::Requested);
    CHECK(events.cancelled[0].cancelledQuantity == 10);
    CHECK(engine.book().empty());

    engine.process(Command::cancel(5, 4, 1));
    REQUIRE(events.rejected.size() == 2);
    CHECK(events.rejected[1].reason == RejectReason::UnknownOrder);
}

TEST_CASE("cancel of a partially filled order removes only the remainder")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());
    engine.process(Command::newLimit(1, 1, Side::Sell, 100, 10)); // order 1
    engine.process(Command::newLimit(2, 1, Side::Buy, 100, 4));   // fills 4
    engine.process(Command::cancel(1, 2, 1));
    REQUIRE(events.cancelled.size() == 1);
    CHECK(events.cancelled[0].cancelledQuantity == 6);
}

TEST_CASE("invalid commands are rejected without side effects")
{
    RecordingListener events;
    MatchingEngine engine(events, checked());

    engine.process(Command::newLimit(1, 1, Side::Buy, 100, 0));
    engine.process(Command::newLimit(1, 2, Side::Buy, 0, 5));
    engine.process(Command::newMarket(1, 3, Side::Buy, -1));
    Command garbage = Command::newLimit(1, 4, Side::Buy, 100, 5);
    garbage.type = static_cast<agora::CommandType>(99);
    engine.process(garbage);

    REQUIRE(events.rejected.size() == 4);
    CHECK(events.rejected[0].reason == RejectReason::InvalidQuantity);
    CHECK(events.rejected[1].reason == RejectReason::InvalidPrice);
    CHECK(events.rejected[2].reason == RejectReason::InvalidQuantity);
    CHECK(events.rejected[3].reason == RejectReason::InvalidCommand);
    CHECK(events.accepted.empty());
    CHECK(engine.book().empty());

    // Rejected commands still consume a sequence number but no order id.
    engine.process(Command::newLimit(1, 5, Side::Buy, 100, 5));
    CHECK(events.accepted[0].sequence == 5);
    CHECK(events.accepted[0].orderId == 1);
}

TEST_CASE("stats track volume")
{
    agora::NullListener sink;
    MatchingEngine engine(sink, checked());
    engine.process(Command::newLimit(1, 1, Side::Sell, 100, 10));
    engine.process(Command::newLimit(2, 1, Side::Buy, 100, 4));
    engine.process(Command::newMarket(3, 1, Side::Buy, 10));

    const auto& stats = engine.stats();
    CHECK(stats.commands == 3);
    CHECK(stats.accepted == 3);
    CHECK(stats.trades == 2);
    CHECK(stats.tradedVolume == 10);
    CHECK(stats.cancelledVolume == 4);
    CHECK(stats.acceptedVolume == 24);
    CHECK_NOTHROW(engine.verify());
}
