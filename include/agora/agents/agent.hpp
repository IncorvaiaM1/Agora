/**
 * @file agent.hpp
 * @brief Agent: base class of every trading strategy, and AgentRunner, which
 *        connects one agent to the exchange.
 */
#pragma once

#include "agora/core/rng.hpp"
#include "agora/core/types.hpp"
#include "agora/net/client_connection.hpp"
#include "agora/net/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <map>

namespace agora::agents
{

/// How an agent places and cancels orders. Implemented by AgentRunner (and by test doubles).
class OrderSink
{
  public:
    virtual ~OrderSink() = default;

    /// Submits a limit order. @return the client order id that the reply will echo.
    virtual ClientOrderId limit(Side side, Price price, Quantity quantity) = 0;

    /// Submits a market order. @return the client order id that the reply will echo.
    virtual ClientOrderId market(Side side, Quantity quantity) = 0;

    /// Asks the exchange to cancel one of this agent's resting orders.
    virtual void cancel(OrderId orderId) = 0;
};

/// One of the agent's own orders that the exchange has accepted and not yet finished.
struct OpenOrder
{
    Side side = Side::Buy;
    Price price = 0;
    Quantity remaining = 0;
    std::uint64_t tick = 0; ///< Tick during which the exchange accepted it (for order lifetimes).
};

/**
 * @brief Base class for trading strategies.
 *
 * The base class does the bookkeeping every strategy needs: it follows the
 * exchange's replies to keep the agent's open orders, share position and cash
 * up to date. A strategy only overrides onTick() to decide what to do, and
 * optionally the notification hooks.
 *
 * Determinism: an agent's decisions may depend only on its seed and on the
 * messages it has received, never on wall-clock time. All randomness comes
 * from rng(). Open orders are kept in an ordered map so iteration order is
 * the same on every platform.
 */
class Agent
{
  public:
    /**
     * @param id   Agent id announced to the exchange; unique per session.
     * @param seed Session seed. It is mixed with the id, so agents sharing a
     *             session seed still draw independent random streams.
     */
    Agent(AgentId id, std::uint64_t seed);
    virtual ~Agent() = default;

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    /// Short strategy name for logs ("zi", "trend", ...).
    virtual const char* name() const noexcept = 0;

    /// Called by AgentRunner once per tick: records the snapshot, then runs the strategy's onTick().
    void step(const net::Tick& tick, OrderSink& orders);

    /// The most recent tick snapshot (all zeros before the first tick).
    const net::Tick& lastTick() const noexcept { return m_lastTick; }

    // Message entry points, called by AgentRunner. Each updates the
    // bookkeeping and then calls the matching protected hook.
    void handle(const net::Accepted& accepted);
    void handle(const net::Rejected& rejected);
    void handle(const net::Cancelled& cancelled);
    void handle(const net::Trade& trade);

    AgentId id() const noexcept { return m_id; }

    /// Net shares held: positive is long, negative is short.
    Quantity position() const noexcept { return m_position; }

    /// Cash in price ticks: falls when buying, rises when selling. Starts at 0.
    std::int64_t cash() const noexcept { return m_cash; }

    /// Cash plus the position valued at @p price.
    std::int64_t markToMarket(Price price) const noexcept { return m_cash + m_position * price; }

    /// The agent's live orders, keyed by exchange order id (oldest first).
    const std::map<OrderId, OpenOrder>& openOrders() const noexcept { return m_openOrders; }

    std::uint64_t tradeCount() const noexcept { return m_tradeCount; }

  protected:
    /// The strategy: called once per tick with the market snapshot. Place and cancel orders through @p orders.
    virtual void onTick(const net::Tick& tick, OrderSink& orders) = 0;

    virtual void onAccepted(const net::Accepted& /*accepted*/) {}
    virtual void onRejected(const net::Rejected& /*rejected*/) {}
    virtual void onCancelled(const net::Cancelled& /*cancelled*/) {}
    /// Called for every trade the agent sees, its own and (if subscribed) everyone else's.
    virtual void onTrade(const net::Trade& /*trade*/) {}

    Rng& rng() noexcept { return m_rng; }

    /**
     * @brief Best single estimate of the current price from a tick snapshot.
     *
     * Mid price if both sides are quoted, otherwise the last trade price,
     * otherwise (empty book, nothing traded yet) the fundamental value.
     */
    static Price referencePrice(const net::Tick& tick) noexcept;

    /// Cancels every open order accepted more than @p age ticks ago. @return how many.
    std::size_t cancelOlderThan(std::uint64_t age, OrderSink& orders) const;

  private:
    /// Applies a fill of @p quantity to one of our orders.
    void applyFill(OrderId orderId, Side side, Price price, Quantity quantity);

    const AgentId m_id;
    Rng m_rng;
    net::Tick m_lastTick;
    std::map<OrderId, OpenOrder> m_openOrders;
    Quantity m_position = 0;
    std::int64_t m_cash = 0;
    std::uint64_t m_tradeCount = 0;
};

/// What AgentRunner::run() reports when the session ends.
struct RunSummary
{
    std::uint64_t ticks = 0;         ///< Ticks the agent acted on.
    std::uint64_t ordersSent = 0;    ///< New orders submitted.
    std::uint64_t cancelsSent = 0;   ///< Cancel requests submitted.
    Price lastPrice = 0;             ///< Last trade price seen in a Tick (0 if none).
    bool endedCleanly = false;       ///< True if the exchange sent End.
};

/**
 * @brief Drives one Agent over one ClientConnection.
 *
 * Performs the handshake, then loops: read a message, update the agent, and
 * on every Tick let the agent act, send its orders in one write and finish
 * with TickDone. The same loop serves live and lockstep mode; in live mode
 * the exchange simply does not wait for the TickDone.
 */
class AgentRunner final : public OrderSink
{
  public:
    /**
     * @param agent      Strategy to run.
     * @param connection Open connection to the exchange.
     * @param marketData Subscribe to all trades (needed by strategies that watch the tape).
     */
    AgentRunner(Agent& agent, net::ClientConnection& connection, bool marketData = false);

    /**
     * @brief Runs until the exchange ends the session or the connection drops.
     * @throws std::runtime_error if the exchange refuses the handshake.
     */
    RunSummary run();

    ClientOrderId limit(Side side, Price price, Quantity quantity) override;
    ClientOrderId market(Side side, Quantity quantity) override;
    void cancel(OrderId orderId) override;

  private:
    Agent& m_agent;
    net::ClientConnection& m_connection;
    bool m_marketData;
    ClientOrderId m_nextClientOrderId = 1;
    RunSummary m_summary;
};

} // namespace agora::agents
