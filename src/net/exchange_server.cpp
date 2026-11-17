/**
 * @file exchange_server.cpp
 * @brief Implementation of ExchangeServer: the engine thread and its two run modes.
 */
#include "agora/net/exchange_server.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace agora::net
{
namespace
{

using Clock = std::chrono::steady_clock;

/// Longest the engine thread sleeps before re-checking the stop flag.
constexpr std::chrono::microseconds kMaxWait{50'000};

/// Messages handled in a row before replies are flushed to the sockets.
constexpr int kBatchSize = 256;

} // namespace

ExchangeServer::ExchangeServer(ExchangeConfig config)
    : m_config(std::move(config)),
      m_channel(makeChannel(m_config.queue, m_config.gateway.maxSessions, m_config.queueCapacity)),
      m_gateway(*m_channel, m_config.gateway),
      m_engine(m_fanout, EngineConfig{m_config.checkInvariants}),
      m_fundamentalRng(m_config.seed),
      m_orderRng(m_config.seed ^ 0xA5A5A5A5A5A5A5A5ULL),
      m_fundamental(m_config.initialFundamental)
{
    if (m_config.lockstep && (m_config.agents == 0 || m_config.ticks == 0))
    {
        throw std::invalid_argument("lockstep mode needs a fixed number of agents and ticks");
    }

    // Trade log first, so a trade is on disk before anyone is told about it.
    if (m_config.tradeLog != nullptr)
    {
        m_tradeLog = std::make_unique<TradeLogWriter>(*m_config.tradeLog);
        m_fanout.add(*m_tradeLog);
    }
    if (m_config.commandLog != nullptr)
    {
        m_commandLog = std::make_unique<CommandLogWriter>(*m_config.commandLog);
    }
    m_fanout.add(*this);

    if (m_config.tickLog != nullptr)
    {
        *m_config.tickLog << "tick,fundamental,bid_price,bid_quantity,ask_price,ask_quantity,last_trade_price,"
                             "trades\n";
    }
}

ExchangeServer::~ExchangeServer()
{
    m_gateway.stop();
}

void ExchangeServer::start()
{
    m_gateway.start();
}

void ExchangeServer::run()
{
    if (m_config.lockstep)
    {
        runLockstep();
    }
    else
    {
        runLive();
    }

    // Tell everyone the session is over, then close the connections.
    m_feed.publishEnd(End{m_tick});
    m_feed.flush();
    m_gateway.stop();
}

// ---------------------------------------------------------------------------
// Run modes
// ---------------------------------------------------------------------------

bool ExchangeServer::waitForAgents()
{
    InboundMessage message;
    while (m_agentSlots.size() < m_config.agents)
    {
        if (m_stop.load(std::memory_order_acquire))
        {
            return false;
        }
        if (m_channel->pop(message, kMaxWait))
        {
            handle(message);
            m_feed.flush();
        }
    }
    return true;
}

void ExchangeServer::runLive()
{
    if (!waitForAgents())
    {
        return;
    }

    const bool ticksEnabled = m_config.tickMillis > 0;
    const auto period = std::chrono::milliseconds(m_config.tickMillis);
    Clock::time_point nextTick = Clock::now() + period;

    InboundMessage message;
    while (!m_stop.load(std::memory_order_acquire))
    {
        // Sleep on the channel until a message arrives or the next tick is due.
        std::chrono::microseconds wait = kMaxWait;
        if (ticksEnabled)
        {
            const auto untilTick =
                std::chrono::duration_cast<std::chrono::microseconds>(nextTick - Clock::now());
            wait = std::clamp(untilTick, std::chrono::microseconds(0), kMaxWait);
        }

        if (m_channel->pop(message, wait))
        {
            // Drain a batch, then answer every client with a single write each.
            handle(message);
            for (int i = 1; i < kBatchSize && m_channel->pop(message, std::chrono::microseconds(0)); ++i)
            {
                handle(message);
            }
            m_feed.flush();
        }

        if (ticksEnabled && Clock::now() >= nextTick)
        {
            publishTick();
            m_feed.flush();
            nextTick += period;
            if (m_config.ticks != 0 && m_tick >= m_config.ticks)
            {
                break;
            }
        }
    }
}

void ExchangeServer::runLockstep()
{
    if (!waitForAgents())
    {
        return;
    }
    m_ticking = true;

    InboundMessage message;
    std::vector<AgentId> order;
    while (m_tick < m_config.ticks && !m_agentSlots.empty() && !m_stop.load(std::memory_order_acquire))
    {
        // 1. Everyone sees the same snapshot.
        publishTick();
        m_feed.flush();

        // 2. Buffer orders until every agent has said it is done with this tick.
        m_waiting.clear();
        for (const auto& entry : m_agentSlots)
        {
            m_waiting.insert(entry.first);
        }
        while (!m_waiting.empty() && !m_stop.load(std::memory_order_acquire))
        {
            if (m_channel->pop(message, kMaxWait))
            {
                handle(message);
            }
        }

        // 3. Match the buffered orders. The agent order is a seeded shuffle of the
        //    sorted ids, so no agent is always first and nothing depends on which
        //    socket happened to deliver first.
        order.clear();
        for (const auto& entry : m_pending)
        {
            if (!entry.second.empty())
            {
                order.push_back(entry.first);
            }
        }
        for (std::size_t i = order.size(); i > 1; --i)
        {
            const auto j = static_cast<std::size_t>(m_orderRng.below(i));
            std::swap(order[i - 1], order[j]);
        }
        for (const AgentId agent : order)
        {
            std::vector<Command>& commands = m_pending[agent];
            for (const Command& command : commands)
            {
                submit(command);
            }
            commands.clear();
        }

        // 4. Clean up after agents that left during the tick, in id order.
        std::sort(m_departed.begin(), m_departed.end());
        for (const AgentId agent : m_departed)
        {
            cancelOpenOrders(agent);
            m_pending.erase(agent);
        }
        m_departed.clear();

        m_feed.flush();
    }
}

// ---------------------------------------------------------------------------
// Inbound messages
// ---------------------------------------------------------------------------

void ExchangeServer::handle(const InboundMessage& message)
{
    switch (message.kind)
    {
    case InboundKind::Connect:
        handleConnect(message);
        break;
    case InboundKind::Disconnect:
        handleDisconnect(message);
        break;
    case InboundKind::Order:
        handleOrder(message);
        break;
    case InboundKind::TickDone:
    {
        const Session& session = m_gateway.session(message.slot);
        if (session.registered() && !session.isObserver() && message.tick == m_tick)
        {
            m_waiting.erase(session.agent());
        }
        break;
    }
    }
}

void ExchangeServer::handleConnect(const InboundMessage& message)
{
    Session& session = m_gateway.session(message.slot);
    const bool observer = (message.flags & kFlagObserver) != 0;

    WelcomeStatus status = WelcomeStatus::Ok;
    if (message.version != kProtocolVersion)
    {
        status = WelcomeStatus::BadVersion;
    }
    else if (!observer && m_agentSlots.count(message.agent) != 0)
    {
        status = WelcomeStatus::DuplicateAgent;
    }
    else if (!observer && m_ticking)
    {
        status = WelcomeStatus::SessionClosed;
    }

    session.send(Welcome{message.agent, status, m_config.lockstep});
    if (status != WelcomeStatus::Ok)
    {
        // Refuse: the reader thread will see the shutdown and report a Disconnect.
        session.flush();
        session.kill();
        return;
    }

    session.registerAs(message.agent, message.flags);
    m_feed.add(session);
    if (!observer)
    {
        m_agentSlots[message.agent] = message.slot;
    }
}

void ExchangeServer::handleDisconnect(const InboundMessage& message)
{
    Session& session = m_gateway.session(message.slot);
    if (session.registered())
    {
        m_feed.remove(session);
        if (!session.isObserver())
        {
            const AgentId agent = session.agent();
            m_agentSlots.erase(agent);
            if (m_ticking)
            {
                // Lockstep: do not wait for it, and tidy up at the tick boundary.
                m_waiting.erase(agent);
                m_departed.push_back(agent);
            }
            else
            {
                cancelOpenOrders(agent);
                m_pending.erase(agent);
            }
        }
    }
    // The reader may have dropped the client itself (malformed frame); make sure
    // the peer sees the connection close now rather than when the slot is reused.
    session.kill();
    m_gateway.release(message.slot);
}

void ExchangeServer::handleOrder(const InboundMessage& message)
{
    const Session& session = m_gateway.session(message.slot);
    if (!session.registered() || session.isObserver())
    {
        return; // Observers cannot trade.
    }

    // The agent id comes from the handshake, never from the order itself, so a
    // client cannot trade or cancel in another agent's name.
    Command command = message.command;
    command.agent = session.agent();

    if (m_config.lockstep)
    {
        m_pending[command.agent].push_back(command);
    }
    else
    {
        submit(command);
    }
}

void ExchangeServer::submit(const Command& command)
{
    if (m_commandLog)
    {
        m_commandLog->write(command);
    }
    m_engine.process(command);
}

void ExchangeServer::cancelOpenOrders(AgentId agent)
{
    const auto found = m_openOrders.find(agent);
    if (found == m_openOrders.end())
    {
        return;
    }
    // Move the list out first: each cancel re-enters the listener callbacks.
    const std::vector<OrderId> ids = std::move(found->second.ids);
    m_openOrders.erase(found);
    for (const OrderId id : ids)
    {
        if (m_engine.book().find(id) != nullptr)
        {
            submit(Command::cancel(agent, 0, id));
        }
    }
}

void ExchangeServer::publishTick()
{
    ++m_tick;
    m_fundamental += m_fundamentalRng.between(-m_config.fundamentalStep, m_config.fundamentalStep);
    m_fundamental = std::max<Price>(m_fundamental, 1);

    Tick tick;
    tick.tick = m_tick;
    tick.fundamental = m_fundamental;
    const OrderBook& book = m_engine.book();
    if (const std::optional<Price> bid = book.bestBid())
    {
        tick.bidPrice = *bid;
        tick.bidQuantity = book.volumeAt(Side::Buy, *bid);
    }
    if (const std::optional<Price> ask = book.bestAsk())
    {
        tick.askPrice = *ask;
        tick.askQuantity = book.volumeAt(Side::Sell, *ask);
    }
    tick.lastTradePrice = m_feed.lastTradePrice();
    m_feed.publishTick(tick);

    if (m_config.tickLog != nullptr)
    {
        // Integers only, like the trade log, so the file is identical on every platform.
        // The last column is the running trade count, which gives volume per tick by differencing.
        *m_config.tickLog << tick.tick << ',' << tick.fundamental << ',' << tick.bidPrice << ','
                          << tick.bidQuantity << ',' << tick.askPrice << ',' << tick.askQuantity << ','
                          << tick.lastTradePrice << ',' << m_engine.stats().trades << '\n';
    }
}

Session* ExchangeServer::sessionOf(AgentId agent)
{
    const auto found = m_agentSlots.find(agent);
    return found == m_agentSlots.end() ? nullptr : &m_gateway.session(found->second);
}

// ---------------------------------------------------------------------------
// Engine events
// ---------------------------------------------------------------------------

void ExchangeServer::onAccepted(const AcceptedEvent& event)
{
    if (event.orderType == OrderType::Limit)
    {
        OpenOrders& open = m_openOrders[event.agent];
        if (open.ids.size() >= open.compactAt)
        {
            // Forget orders that have since been filled or cancelled. This runs before
            // the new id is added because the new order is not on the book yet.
            const OrderBook& book = m_engine.book();
            open.ids.erase(std::remove_if(open.ids.begin(), open.ids.end(),
                                          [&book](OrderId id) { return book.find(id) == nullptr; }),
                           open.ids.end());
            open.compactAt = std::max<std::size_t>(1024, 2 * open.ids.size());
        }
        open.ids.push_back(event.orderId);
    }

    if (Session* session = sessionOf(event.agent))
    {
        Accepted accepted;
        accepted.sequence = event.sequence;
        accepted.orderId = event.orderId;
        accepted.clientOrderId = event.clientOrderId;
        accepted.side = event.side;
        accepted.orderType = event.orderType;
        accepted.price = event.price;
        accepted.quantity = event.quantity;
        session->send(accepted);
    }
}

void ExchangeServer::onRejected(const RejectedEvent& event)
{
    if (Session* session = sessionOf(event.agent))
    {
        session->send(Rejected{event.sequence, event.clientOrderId, event.reason});
    }
}

void ExchangeServer::onCancelled(const CancelledEvent& event)
{
    if (Session* session = sessionOf(event.agent))
    {
        session->send(Cancelled{event.sequence, event.orderId, event.cancelledQuantity, event.reason});
    }
}

void ExchangeServer::onTrade(const TradeEvent& event)
{
    m_feed.publishTrade(event);
}

} // namespace agora::net
