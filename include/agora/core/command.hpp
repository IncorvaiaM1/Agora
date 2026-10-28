/**
 * @file command.hpp
 * @brief Command: the only input the matching engine accepts.
 *
 * Every agent action (new order or cancel) reaches the engine as a Command.
 * Because the engine is a deterministic function of its ordered command
 * stream, logging Commands in the order they were sequenced is enough to
 * replay a whole session exactly.
 */
#pragma once

#include "agora/core/types.hpp"

namespace agora
{

/// Kind of request carried by a Command.
enum class CommandType : std::uint8_t
{
    NewOrder = 1, ///< Submit a limit or market order.
    Cancel = 2    ///< Cancel a resting order by its exchange OrderId.
};

/**
 * @brief A request from an agent to the matching engine.
 *
 * A plain aggregate so it can be copied through queues and serialized
 * field by field. Use the named constructors to build valid commands.
 */
struct Command
{
    CommandType type = CommandType::NewOrder;
    AgentId agent = 0;
    ClientOrderId clientOrderId = 0;
    Side side = Side::Buy;
    OrderType orderType = OrderType::Limit;
    Price price = 0;           ///< Limit price in ticks; ignored for market orders.
    Quantity quantity = 0;     ///< Shares; ignored for cancels.
    OrderId targetOrderId = 0; ///< Order to cancel; ignored for new orders.

    /// Builds a new limit order request.
    static Command newLimit(AgentId agent, ClientOrderId clientId, Side side, Price price, Quantity quantity)
    {
        return Command{CommandType::NewOrder, agent, clientId, side, OrderType::Limit, price, quantity, 0};
    }

    /// Builds a new market order request.
    static Command newMarket(AgentId agent, ClientOrderId clientId, Side side, Quantity quantity)
    {
        return Command{CommandType::NewOrder, agent, clientId, side, OrderType::Market, 0, quantity, 0};
    }

    /// Builds a cancel request for a resting order owned by @p agent.
    static Command cancel(AgentId agent, ClientOrderId clientId, OrderId target)
    {
        return Command{CommandType::Cancel, agent, clientId, Side::Buy, OrderType::Limit, 0, 0, target};
    }

    bool operator==(const Command& other) const noexcept
    {
        return type == other.type && agent == other.agent && clientOrderId == other.clientOrderId &&
               side == other.side && orderType == other.orderType && price == other.price &&
               quantity == other.quantity && targetOrderId == other.targetOrderId;
    }
    bool operator!=(const Command& other) const noexcept { return !(*this == other); }
};

} // namespace agora
