// Agent bookkeeping and the four strategies, tested without any sockets:
// a fake OrderSink records what each agent tries to do.
#include "agora/agents/agent_factory.hpp"
#include "agora/agents/market_maker_agent.hpp"
#include "agora/agents/trend_agent.hpp"
#include "agora/agents/value_agent.hpp"
#include "agora/agents/zero_intelligence_agent.hpp"

#include "doctest/doctest.h"

#include <string>
#include <vector>

using namespace agora;
using namespace agora::agents;

namespace
{

/// Records orders instead of sending them, and can play the exchange's part.
class FakeSink final : public OrderSink
{
  public:
    struct Sent
    {
        ClientOrderId clientOrderId;
        Side side;
        OrderType type;
        Price price;
        Quantity quantity;
    };

    ClientOrderId limit(Side side, Price price, Quantity quantity) override
    {
        orders.push_back(Sent{m_nextClientId, side, OrderType::Limit, price, quantity});
        return m_nextClientId++;
    }
    ClientOrderId market(Side side, Quantity quantity) override
    {
        orders.push_back(Sent{m_nextClientId, side, OrderType::Market, 0, quantity});
        return m_nextClientId++;
    }
    void cancel(OrderId orderId) override { cancels.push_back(orderId); }

    void clear()
    {
        orders.clear();
        cancels.clear();
    }

    /// Acknowledges every recorded order as resting, the way the exchange would.
    void acceptAll(Agent& agent)
    {
        for (const Sent& sent : orders)
        {
            net::Accepted accepted;
            accepted.orderId = m_nextOrderId++;
            accepted.clientOrderId = sent.clientOrderId;
            accepted.side = sent.side;
            accepted.orderType = sent.type;
            accepted.price = sent.price;
            accepted.quantity = sent.quantity;
            agent.handle(accepted);
        }
    }

    Quantity total(Side side) const
    {
        Quantity sum = 0;
        for (const Sent& sent : orders)
        {
            sum += sent.side == side ? sent.quantity : 0;
        }
        return sum;
    }

    std::vector<Sent> orders;
    std::vector<OrderId> cancels;

  private:
    ClientOrderId m_nextClientId = 1;
    OrderId m_nextOrderId = 1000;
};

/// A tick with a two-sided book centred on @p mid.
net::Tick tickAt(std::uint64_t number, Price mid, Price fundamental)
{
    net::Tick tick;
    tick.tick = number;
    tick.fundamental = fundamental;
    tick.bidPrice = mid - 1;
    tick.bidQuantity = 10;
    tick.askPrice = mid + 1;
    tick.askQuantity = 10;
    tick.lastTradePrice = mid;
    return tick;
}

net::Trade tradeBetween(AgentId buyer, OrderId buyOrder, AgentId seller, OrderId sellOrder, Price price,
                        Quantity quantity)
{
    net::Trade trade;
    trade.buyAgent = buyer;
    trade.buyOrderId = buyOrder;
    trade.sellAgent = seller;
    trade.sellOrderId = sellOrder;
    trade.price = price;
    trade.quantity = quantity;
    return trade;
}

} // namespace

TEST_CASE("agent: position, cash and open orders follow the exchange's messages")
{
    ZeroIntelligenceAgent agent(1, 42);

    net::Accepted buy;
    buy.orderId = 10;
    buy.side = Side::Buy;
    buy.price = 100;
    buy.quantity = 5;
    agent.handle(buy);
    REQUIRE(agent.openOrders().size() == 1);

    // Partial fill: the order stays open with less remaining.
    agent.handle(tradeBetween(1, 10, 2, 99, 100, 2));
    CHECK(agent.position() == 2);
    CHECK(agent.cash() == -200);
    CHECK(agent.openOrders().at(10).remaining == 3);

    // Someone else's trade changes nothing.
    agent.handle(tradeBetween(3, 50, 4, 51, 500, 7));
    CHECK(agent.position() == 2);

    // Final fill removes the order.
    agent.handle(tradeBetween(1, 10, 2, 98, 100, 3));
    CHECK(agent.position() == 5);
    CHECK(agent.cash() == -500);
    CHECK(agent.openOrders().empty());

    // Selling at a higher price: flat again, with a profit left in cash.
    net::Accepted sell;
    sell.orderId = 11;
    sell.side = Side::Sell;
    sell.price = 110;
    sell.quantity = 5;
    agent.handle(sell);
    agent.handle(tradeBetween(2, 97, 1, 11, 110, 5));
    CHECK(agent.position() == 0);
    CHECK(agent.cash() == 50);
    CHECK(agent.markToMarket(12345) == 50);
    CHECK(agent.tradeCount() == 3);

    // A cancel removes an open order; trading with itself nets to nothing.
    buy.orderId = 12;
    agent.handle(buy);
    agent.handle(net::Cancelled{0, 12, 5, CancelReason::Requested});
    CHECK(agent.openOrders().empty());
    agent.handle(tradeBetween(1, 20, 1, 21, 100, 4));
    CHECK(agent.position() == 0);
    CHECK(agent.cash() == 50);
}

TEST_CASE("agent: same id and seed give the same decisions; different ids differ")
{
    for (const char* type : {"zi", "value", "trend", "mm"})
    {
        CAPTURE(type);
        FakeSink sinkA;
        FakeSink sinkB;
        FakeSink sinkC;
        const auto a = makeAgent(type, 5, 99);
        const auto b = makeAgent(type, 5, 99);
        const auto c = makeAgent(type, 6, 99);
        REQUIRE(a);
        // A price path with a rise and a fall, so every strategy has something to react to.
        for (std::uint64_t t = 1; t <= 400; ++t)
        {
            const Price mid = 10'000 + (t < 200 ? static_cast<Price>(t) : static_cast<Price>(400 - t));
            const net::Tick tick = tickAt(t, mid, 10'050);
            a->step(tick, sinkA);
            b->step(tick, sinkB);
            c->step(tick, sinkC);
        }
        REQUIRE(sinkA.orders.size() == sinkB.orders.size());
        bool same = !sinkA.orders.empty();
        for (std::size_t i = 0; i < sinkA.orders.size(); ++i)
        {
            same = same && sinkA.orders[i].side == sinkB.orders[i].side &&
                   sinkA.orders[i].price == sinkB.orders[i].price &&
                   sinkA.orders[i].quantity == sinkB.orders[i].quantity;
        }
        CHECK(same);

        // The market maker uses no randomness, so only the random strategies must differ by id.
        if (std::string(type) != "mm")
        {
            bool differs = sinkA.orders.size() != sinkC.orders.size();
            for (std::size_t i = 0; !differs && i < sinkA.orders.size(); ++i)
            {
                differs = sinkA.orders[i].side != sinkC.orders[i].side ||
                          sinkA.orders[i].price != sinkC.orders[i].price ||
                          sinkA.orders[i].quantity != sinkC.orders[i].quantity;
            }
            CHECK(differs);
        }
    }
    CHECK(makeAgent("nonsense", 1, 1) == nullptr);
}

TEST_CASE("zero intelligence: prices stay in the configured window around the mid")
{
    ZeroIntelligenceConfig config;
    ZeroIntelligenceAgent agent(1, 7, config);
    FakeSink sink;
    for (std::uint64_t t = 1; t <= 2000; ++t)
    {
        agent.step(tickAt(t, 10'000, 10'000), sink);
    }
    REQUIRE(sink.orders.size() > 500);
    Quantity limits = 0;
    Quantity markets = 0;
    for (const FakeSink::Sent& sent : sink.orders)
    {
        CHECK(sent.quantity >= 1);
        CHECK(sent.quantity <= config.maxQuantity);
        if (sent.type == OrderType::Limit)
        {
            ++limits;
            const Price behind = sent.side == Side::Buy ? 10'000 - sent.price : sent.price - 10'000;
            CHECK(behind >= -config.aggression);
            CHECK(behind <= config.depth);
        }
        else
        {
            ++markets;
        }
    }
    CHECK(limits > markets); // mostly liquidity providing
    CHECK(markets > 0);
    // No view on direction: buys and sells are roughly balanced.
    CHECK(sink.total(Side::Buy) > sink.total(Side::Sell) / 2);
    CHECK(sink.total(Side::Sell) > sink.total(Side::Buy) / 2);
}

TEST_CASE("value: buys when the market is cheap, sells when it is rich, idles when fair")
{
    ValueConfig config;
    const Price edge = config.maxBias + config.noise + config.margin; // beyond this the sign is certain

    SUBCASE("cheap market")
    {
        ValueAgent agent(1, 3, config);
        FakeSink sink;
        for (std::uint64_t t = 1; t <= 300; ++t)
        {
            agent.step(tickAt(t, 10'000, 10'000 + edge + 20), sink);
        }
        REQUIRE(sink.orders.size() > 30);
        CHECK(sink.total(Side::Sell) == 0);
        for (const FakeSink::Sent& sent : sink.orders)
        {
            CHECK(sent.type == OrderType::Limit);
            CHECK(sent.price > 10'000); // crosses the current market...
            // ...but never pays more than its highest possible estimate less the margin.
            CHECK(sent.price <= 10'000 + edge + 20 + config.maxBias + config.noise - config.margin);
        }
    }
    SUBCASE("rich market")
    {
        ValueAgent agent(1, 3, config);
        FakeSink sink;
        for (std::uint64_t t = 1; t <= 300; ++t)
        {
            agent.step(tickAt(t, 10'000, 10'000 - edge - 20), sink);
        }
        REQUIRE(sink.orders.size() > 30);
        CHECK(sink.total(Side::Buy) == 0);
    }
    SUBCASE("fair market with no noise")
    {
        config.noise = 0;
        ValueAgent agent(1, 3, config);
        FakeSink sink;
        for (std::uint64_t t = 1; t <= 300; ++t)
        {
            // Offset the fundamental by the agent's own bias, so its estimate equals the price.
            agent.step(tickAt(t, 10'000, 10'000 - agent.bias()), sink);
        }
        CHECK(sink.orders.empty());
    }
    SUBCASE("stops adding at the position limit and cancels stale orders")
    {
        config.positionLimit = 10;
        ValueAgent agent(1, 3, config);
        FakeSink sink;
        const net::Tick cheap = tickAt(1, 10'000, 10'000 + edge + 20);
        while (sink.orders.empty())
        {
            agent.step(cheap, sink);
        }
        sink.acceptAll(agent);
        REQUIRE(agent.openOrders().size() == 1);
        const OrderId resting = agent.openOrders().begin()->first;

        // Fill past the limit: no more buying however cheap the market looks.
        agent.handle(tradeBetween(1, 5000, 2, 5001, 10'000, 10));
        sink.clear();
        for (std::uint64_t t = 2; t <= 200; ++t)
        {
            agent.step(tickAt(t, 10'000, 10'000 + edge + 20), sink);
        }
        CHECK(sink.orders.empty());
        // The order accepted at tick 1 outlived its lifetime and was cancelled.
        REQUIRE_FALSE(sink.cancels.empty());
        CHECK(sink.cancels.front() == resting);
    }
}

TEST_CASE("trend: follows a sustained move and stays out of a flat market")
{
    TrendConfig config;
    config.actProbability = 1.0;

    SUBCASE("windows are drawn inside the configured ranges")
    {
        for (AgentId id = 1; id <= 50; ++id)
        {
            const TrendAgent agent(id, 1, config);
            CHECK(agent.fastWindow() >= config.minFastWindow);
            CHECK(agent.fastWindow() <= config.maxFastWindow);
            CHECK(agent.slowWindow() >= config.minSlowWindow);
            CHECK(agent.slowWindow() <= config.maxSlowWindow);
        }
    }
    SUBCASE("flat price: no signal, no orders")
    {
        TrendAgent agent(1, 1, config);
        FakeSink sink;
        for (std::uint64_t t = 1; t <= 500; ++t)
        {
            agent.step(tickAt(t, 10'000, 10'000), sink);
        }
        CHECK(agent.signal() == 0);
        CHECK(sink.orders.empty());
    }
    SUBCASE("rising price: buys through the offer, and only after the warm-up")
    {
        TrendAgent agent(1, 1, config);
        FakeSink sink;
        std::uint64_t firstOrderTick = 0;
        for (std::uint64_t t = 1; t <= 300; ++t)
        {
            agent.step(tickAt(t, 10'000 + static_cast<Price>(t), 10'000), sink);
            if (firstOrderTick == 0 && !sink.orders.empty())
            {
                firstOrderTick = t;
            }
        }
        CHECK(agent.signal() > config.threshold);
        REQUIRE_FALSE(sink.orders.empty());
        CHECK(firstOrderTick >= static_cast<std::uint64_t>(agent.slowWindow()));
        CHECK(sink.total(Side::Sell) == 0);
        for (const FakeSink::Sent& sent : sink.orders)
        {
            // Marketable, but with a cap on how far it will chase.
            CHECK(sent.type == OrderType::Limit);
            CHECK(sent.price > 10'000);
            CHECK(sent.quantity <= config.maxQuantity);
        }
        const FakeSink::Sent& last = sink.orders.back();
        CHECK(last.price == 10'000 + 300 + 1 + config.maxSlippage); // best ask at tick 300, plus slippage
    }
    SUBCASE("falling price: sells")
    {
        TrendAgent agent(1, 1, config);
        FakeSink sink;
        for (std::uint64_t t = 1; t <= 300; ++t)
        {
            agent.step(tickAt(t, 10'000 - static_cast<Price>(t), 10'000), sink);
        }
        REQUIRE_FALSE(sink.orders.empty());
        CHECK(sink.total(Side::Buy) == 0);
    }
}

TEST_CASE("market maker: quotes both sides, requotes when the price moves, leans against inventory")
{
    MarketMakerConfig config;
    MarketMakerAgent agent(1, 1, config);
    FakeSink sink;

    // First tick: one bid and one ask, a spread apart, around the last trade.
    agent.step(tickAt(1, 10'000, 10'000), sink);
    REQUIRE(sink.orders.size() == 2);
    CHECK(sink.orders[0].side == Side::Buy);
    CHECK(sink.orders[0].price == 10'000 - config.halfSpread);
    CHECK(sink.orders[1].side == Side::Sell);
    CHECK(sink.orders[1].price == 10'000 + config.halfSpread);
    CHECK(sink.orders[0].quantity == config.quoteSize);

    // Unacknowledged quotes are not sent twice.
    sink.orders.clear();
    agent.step(tickAt(2, 10'000, 10'000), sink);
    CHECK(sink.orders.empty());

    // Rebuild with acknowledgements, then: nothing changes, nothing is sent.
    agent.step(tickAt(3, 10'000, 10'000), sink);
    FakeSink quiet;
    MarketMakerAgent steady(2, 1, config);
    steady.step(tickAt(1, 10'000, 10'000), quiet);
    quiet.acceptAll(steady);
    quiet.clear();
    steady.step(tickAt(2, 10'000, 10'000), quiet);
    CHECK(quiet.orders.empty());
    CHECK(quiet.cancels.empty());

    // A move inside the tolerance is ignored.
    steady.step(tickAt(3, 10'002, 10'000), quiet);
    CHECK(quiet.orders.empty());
    CHECK(quiet.cancels.empty());

    // A real move: both old quotes are cancelled (once) and replaced.
    steady.step(tickAt(3, 10'010, 10'010), quiet);
    CHECK(quiet.cancels.size() == 2);
    REQUIRE(quiet.orders.size() == 2);
    CHECK(quiet.orders[0].price == 10'010 - config.halfSpread);
    CHECK(quiet.orders[1].price == 10'010 + config.halfSpread);
    const std::size_t cancelsSoFar = quiet.cancels.size();
    steady.step(tickAt(4, 10'010, 10'010), quiet);
    CHECK(quiet.cancels.size() == cancelsSoFar); // not re-sent while still pending

    // Inventory: a long position lowers the centre, a short one raises it.
    const net::Tick flat = tickAt(5, 10'000, 10'000);
    MarketMakerAgent holder(3, 1, config);
    CHECK(holder.centre(flat) == 10'000);
    // Halfway between the last trade and the fundamental.
    CHECK(holder.centre(tickAt(5, 10'020, 10'000)) == 10'010);
    holder.handle(tradeBetween(3, 1, 9, 2, 10'000, 3 * config.sharesPerSkewTick));
    CHECK(holder.centre(flat) == 10'000 - 3);
    holder.handle(tradeBetween(9, 3, 3, 4, 10'000, 6 * config.sharesPerSkewTick));
    CHECK(holder.centre(flat) == 10'000 + 3);

    // At the position limit it stops bidding but keeps offering.
    MarketMakerAgent full(4, 1, config);
    full.handle(tradeBetween(4, 1, 9, 2, 10'000, config.positionLimit));
    FakeSink oneSided;
    full.step(flat, oneSided);
    REQUIRE(oneSided.orders.size() == 1);
    CHECK(oneSided.orders[0].side == Side::Sell);
}
