// Wire protocol: every message survives an encode/decode round trip, and the
// frame reader copes with the ways TCP can split or merge a byte stream.
#include "agora/net/protocol.hpp"

#include "doctest/doctest.h"

#include <stdexcept>
#include <variant>

using namespace agora;
using namespace agora::net;

namespace
{

/// Encodes one message and returns the single frame it produced.
template <typename Message> Frame frameOf(const Message& message, Bytes& storage, FrameReader& reader)
{
    storage.clear();
    encode(message, storage);
    reader.append(storage.data(), storage.size());
    Frame frame;
    REQUIRE(reader.next(frame));
    REQUIRE(reader.pending() == 0);
    return frame;
}

template <typename Message> Message roundTripClient(const Message& message)
{
    Bytes storage;
    FrameReader reader;
    ClientMessage decoded;
    REQUIRE(decode(frameOf(message, storage, reader), decoded));
    REQUIRE(std::holds_alternative<Message>(decoded));
    return std::get<Message>(decoded);
}

template <typename Message> Message roundTripServer(const Message& message)
{
    Bytes storage;
    FrameReader reader;
    ServerMessage decoded;
    REQUIRE(decode(frameOf(message, storage, reader), decoded));
    REQUIRE(std::holds_alternative<Message>(decoded));
    return std::get<Message>(decoded);
}

} // namespace

TEST_CASE("protocol: client messages round trip")
{
    Hello hello;
    hello.agent = 0xDEADBEEF;
    hello.flags = kFlagMarketData | kFlagObserver;
    const Hello helloBack = roundTripClient(hello);
    CHECK(helloBack.version == kProtocolVersion);
    CHECK(helloBack.agent == 0xDEADBEEF);
    CHECK(helloBack.flags == (kFlagMarketData | kFlagObserver));

    NewOrder order;
    order.clientOrderId = 0x0123456789ABCDEFULL;
    order.side = Side::Sell;
    order.orderType = OrderType::Market;
    order.price = -5; // Nonsense, but the codec must carry negative values faithfully.
    order.quantity = 9'000'000'000LL;
    const NewOrder orderBack = roundTripClient(order);
    CHECK(orderBack.clientOrderId == order.clientOrderId);
    CHECK(orderBack.side == Side::Sell);
    CHECK(orderBack.orderType == OrderType::Market);
    CHECK(orderBack.price == -5);
    CHECK(orderBack.quantity == 9'000'000'000LL);

    const CancelOrder cancelBack = roundTripClient(CancelOrder{7, 99});
    CHECK(cancelBack.clientOrderId == 7);
    CHECK(cancelBack.targetOrderId == 99);

    CHECK(roundTripClient(TickDone{123456789012ULL}).tick == 123456789012ULL);
}

TEST_CASE("protocol: server messages round trip")
{
    const Welcome welcome = roundTripServer(Welcome{42, WelcomeStatus::DuplicateAgent, true});
    CHECK(welcome.agent == 42);
    CHECK(welcome.status == WelcomeStatus::DuplicateAgent);
    CHECK(welcome.lockstep);

    Accepted accepted;
    accepted.sequence = 11;
    accepted.orderId = 22;
    accepted.clientOrderId = 33;
    accepted.side = Side::Sell;
    accepted.orderType = OrderType::Limit;
    accepted.price = 10'001;
    accepted.quantity = 250;
    const Accepted acceptedBack = roundTripServer(accepted);
    CHECK(acceptedBack.sequence == 11);
    CHECK(acceptedBack.orderId == 22);
    CHECK(acceptedBack.clientOrderId == 33);
    CHECK(acceptedBack.side == Side::Sell);
    CHECK(acceptedBack.orderType == OrderType::Limit);
    CHECK(acceptedBack.price == 10'001);
    CHECK(acceptedBack.quantity == 250);

    const Rejected rejected = roundTripServer(Rejected{5, 6, RejectReason::NotOwner});
    CHECK(rejected.sequence == 5);
    CHECK(rejected.clientOrderId == 6);
    CHECK(rejected.reason == RejectReason::NotOwner);

    const Cancelled cancelled = roundTripServer(Cancelled{8, 9, 10, CancelReason::NoLiquidity});
    CHECK(cancelled.sequence == 8);
    CHECK(cancelled.orderId == 9);
    CHECK(cancelled.quantity == 10);
    CHECK(cancelled.reason == CancelReason::NoLiquidity);

    Trade trade;
    trade.sequence = 1;
    trade.tradeId = 2;
    trade.buyOrderId = 3;
    trade.sellOrderId = 4;
    trade.buyAgent = 5;
    trade.sellAgent = 6;
    trade.price = 7;
    trade.quantity = 8;
    trade.aggressor = Side::Sell;
    const Trade tradeBack = roundTripServer(trade);
    CHECK(tradeBack.sequence == 1);
    CHECK(tradeBack.tradeId == 2);
    CHECK(tradeBack.buyOrderId == 3);
    CHECK(tradeBack.sellOrderId == 4);
    CHECK(tradeBack.buyAgent == 5);
    CHECK(tradeBack.sellAgent == 6);
    CHECK(tradeBack.price == 7);
    CHECK(tradeBack.quantity == 8);
    CHECK(tradeBack.aggressor == Side::Sell);

    Tick tick;
    tick.tick = 77;
    tick.fundamental = 10'000;
    tick.bidPrice = 9'999;
    tick.bidQuantity = 12;
    tick.askPrice = 10'002;
    tick.askQuantity = 34;
    tick.lastTradePrice = 10'001;
    const Tick tickBack = roundTripServer(tick);
    CHECK(tickBack.tick == 77);
    CHECK(tickBack.fundamental == 10'000);
    CHECK(tickBack.bidPrice == 9'999);
    CHECK(tickBack.bidQuantity == 12);
    CHECK(tickBack.askPrice == 10'002);
    CHECK(tickBack.askQuantity == 34);
    CHECK(tickBack.lastTradePrice == 10'001);

    CHECK(roundTripServer(End{500}).ticks == 500);
}

TEST_CASE("protocol: encoding is little-endian with a 4 byte header")
{
    Bytes bytes;
    encode(TickDone{0x0102030405060708ULL}, bytes);
    REQUIRE(bytes.size() == kHeaderSize + 8);
    CHECK(bytes[0] == 8); // payload length, low byte
    CHECK(bytes[1] == 0); // payload length, high byte
    CHECK(bytes[2] == static_cast<unsigned char>(MessageType::TickDone));
    CHECK(bytes[3] == 0);    // reserved
    CHECK(bytes[4] == 0x08); // least significant byte first
    CHECK(bytes[11] == 0x01);
}

TEST_CASE("protocol: frames arriving one byte at a time are reassembled")
{
    Bytes stream;
    encode(NewOrder{1, Side::Buy, OrderType::Limit, 100, 5}, stream);
    encode(CancelOrder{2, 1}, stream);
    encode(TickDone{3}, stream);

    FrameReader reader;
    std::vector<MessageType> seen;
    for (const unsigned char byte : stream)
    {
        reader.append(&byte, 1);
        Frame frame;
        while (reader.next(frame))
        {
            ClientMessage message;
            CHECK(decode(frame, message));
            seen.push_back(frame.type);
        }
    }
    REQUIRE(seen.size() == 3);
    CHECK(seen[0] == MessageType::NewOrder);
    CHECK(seen[1] == MessageType::CancelOrder);
    CHECK(seen[2] == MessageType::TickDone);
    CHECK(reader.pending() == 0);
}

TEST_CASE("protocol: several frames in one read are all returned")
{
    Bytes stream;
    for (std::uint64_t tick = 1; tick <= 50; ++tick)
    {
        encode(TickDone{tick}, stream);
    }
    FrameReader reader;
    reader.append(stream.data(), stream.size());

    Frame frame;
    std::uint64_t expected = 1;
    while (reader.next(frame))
    {
        ClientMessage message;
        REQUIRE(decode(frame, message));
        CHECK(std::get<TickDone>(message).tick == expected++);
    }
    CHECK(expected == 51);
}

TEST_CASE("protocol: malformed input is refused")
{
    FrameReader reader;
    Frame frame;

    SUBCASE("wrong direction: a server message is not a client message")
    {
        Bytes bytes;
        encode(End{1}, bytes);
        reader.append(bytes.data(), bytes.size());
        REQUIRE(reader.next(frame));
        ClientMessage message;
        CHECK_FALSE(decode(frame, message));
    }
    SUBCASE("wrong payload length for the type")
    {
        const unsigned char bytes[] = {4, 0, static_cast<unsigned char>(MessageType::TickDone), 0, 1, 2, 3, 4};
        reader.append(bytes, sizeof(bytes));
        REQUIRE(reader.next(frame));
        ClientMessage message;
        CHECK_FALSE(decode(frame, message));
    }
    SUBCASE("unknown enum value")
    {
        Bytes bytes;
        encode(NewOrder{1, Side::Buy, OrderType::Limit, 100, 5}, bytes);
        bytes[kHeaderSize + 8] = 7; // side byte
        reader.append(bytes.data(), bytes.size());
        REQUIRE(reader.next(frame));
        ClientMessage message;
        CHECK_FALSE(decode(frame, message));
    }
    SUBCASE("unknown message type")
    {
        const unsigned char bytes[] = {0, 0, 200, 0};
        reader.append(bytes, sizeof(bytes));
        REQUIRE(reader.next(frame));
        ClientMessage clientMessage;
        ServerMessage serverMessage;
        CHECK_FALSE(decode(frame, clientMessage));
        CHECK_FALSE(decode(frame, serverMessage));
    }
    SUBCASE("oversized frame cannot be resynchronized")
    {
        const unsigned char bytes[] = {0xFF, 0xFF, 1, 0};
        reader.append(bytes, sizeof(bytes));
        CHECK_THROWS_AS(reader.next(frame), std::runtime_error);
    }
}
