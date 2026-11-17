/**
 * @file agent.cpp
 * @brief Implementation of Agent bookkeeping and AgentRunner.
 */
#include "agora/agents/agent.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace agora::agents
{

Agent::Agent(AgentId id, std::uint64_t seed)
    : m_id(id), m_rng(seed ^ (0xD1B54A32D192ED03ULL * (static_cast<std::uint64_t>(id) + 1)))
{
}

void Agent::step(const net::Tick& tick, OrderSink& orders)
{
    m_lastTick = tick;
    onTick(tick, orders);
}

Price Agent::referencePrice(const net::Tick& tick) noexcept
{
    if (tick.bidPrice > 0 && tick.askPrice > 0)
    {
        return (tick.bidPrice + tick.askPrice) / 2;
    }
    if (tick.lastTradePrice > 0)
    {
        return tick.lastTradePrice;
    }
    return tick.fundamental;
}

std::size_t Agent::cancelOlderThan(std::uint64_t age, OrderSink& orders) const
{
    std::size_t cancelled = 0;
    for (const auto& entry : m_openOrders)
    {
        if (m_lastTick.tick > entry.second.tick + age)
        {
            orders.cancel(entry.first);
            ++cancelled;
        }
    }
    return cancelled;
}

void Agent::handle(const net::Accepted& accepted)
{
    // Track every accepted order. One that fills at once is removed again by
    // the Trade messages that follow; a market order's unfilled remainder is
    // removed by the Cancelled message that follows.
    m_openOrders[accepted.orderId] = OpenOrder{accepted.side, accepted.price, accepted.quantity, m_lastTick.tick};
    onAccepted(accepted);
}

void Agent::handle(const net::Rejected& rejected)
{
    onRejected(rejected);
}

void Agent::handle(const net::Cancelled& cancelled)
{
    m_openOrders.erase(cancelled.orderId);
    onCancelled(cancelled);
}

void Agent::handle(const net::Trade& trade)
{
    // An agent can be on both sides of a trade (it crossed its own quote), so
    // the two checks are independent.
    if (trade.buyAgent == m_id)
    {
        applyFill(trade.buyOrderId, Side::Buy, trade.price, trade.quantity);
    }
    if (trade.sellAgent == m_id)
    {
        applyFill(trade.sellOrderId, Side::Sell, trade.price, trade.quantity);
    }
    onTrade(trade);
}

void Agent::applyFill(OrderId orderId, Side side, Price price, Quantity quantity)
{
    ++m_tradeCount;
    if (side == Side::Buy)
    {
        m_position += quantity;
        m_cash -= price * quantity;
    }
    else
    {
        m_position -= quantity;
        m_cash += price * quantity;
    }

    const auto found = m_openOrders.find(orderId);
    if (found != m_openOrders.end())
    {
        found->second.remaining -= quantity;
        if (found->second.remaining <= 0)
        {
            m_openOrders.erase(found);
        }
    }
}

AgentRunner::AgentRunner(Agent& agent, net::ClientConnection& connection, bool marketData)
    : m_agent(agent), m_connection(connection), m_marketData(marketData)
{
}

RunSummary AgentRunner::run()
{
    net::Hello hello;
    hello.agent = m_agent.id();
    hello.flags = m_marketData ? net::kFlagMarketData : 0;
    m_connection.send(hello);
    m_connection.flush();

    net::ServerMessage message;
    if (!m_connection.receive(message) || !std::holds_alternative<net::Welcome>(message))
    {
        throw std::runtime_error("exchange closed the connection during the handshake");
    }
    const net::Welcome& welcome = std::get<net::Welcome>(message);
    if (welcome.status != net::WelcomeStatus::Ok)
    {
        throw std::runtime_error("exchange refused agent " + std::to_string(m_agent.id()) + " (status " +
                                 std::to_string(static_cast<int>(welcome.status)) + ")");
    }

    bool running = true;
    while (running && m_connection.receive(message))
    {
        std::visit(
            [this, &running](const auto& received)
            {
                using Message = std::decay_t<decltype(received)>;
                if constexpr (std::is_same_v<Message, net::Tick>)
                {
                    m_summary.lastPrice = received.lastTradePrice;
                    ++m_summary.ticks;
                    m_agent.step(received, *this);
                    // TickDone goes out in the same write as this tick's orders.
                    m_connection.send(net::TickDone{received.tick});
                    running = m_connection.flush();
                }
                else if constexpr (std::is_same_v<Message, net::End>)
                {
                    m_summary.endedCleanly = true;
                    running = false;
                }
                else if constexpr (std::is_same_v<Message, net::Welcome>)
                {
                    // Only valid during the handshake; ignore a stray one.
                }
                else
                {
                    m_agent.handle(received);
                }
            },
            message);
    }
    return m_summary;
}

ClientOrderId AgentRunner::limit(Side side, Price price, Quantity quantity)
{
    net::NewOrder order;
    order.clientOrderId = m_nextClientOrderId++;
    order.side = side;
    order.orderType = OrderType::Limit;
    order.price = price;
    order.quantity = quantity;
    m_connection.send(order);
    ++m_summary.ordersSent;
    return order.clientOrderId;
}

ClientOrderId AgentRunner::market(Side side, Quantity quantity)
{
    net::NewOrder order;
    order.clientOrderId = m_nextClientOrderId++;
    order.side = side;
    order.orderType = OrderType::Market;
    order.quantity = quantity;
    m_connection.send(order);
    ++m_summary.ordersSent;
    return order.clientOrderId;
}

void AgentRunner::cancel(OrderId orderId)
{
    m_connection.send(net::CancelOrder{m_nextClientOrderId++, orderId});
    ++m_summary.cancelsSent;
}

} // namespace agora::agents
