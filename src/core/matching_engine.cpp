/**
 * @file matching_engine.cpp
 * @brief Implementation of MatchingEngine and the event name helpers.
 */
#include "agora/core/matching_engine.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace agora
{

const char* rejectReasonName(RejectReason reason) noexcept
{
    switch (reason)
    {
    case RejectReason::InvalidQuantity:
        return "INVALID_QUANTITY";
    case RejectReason::InvalidPrice:
        return "INVALID_PRICE";
    case RejectReason::UnknownOrder:
        return "UNKNOWN_ORDER";
    case RejectReason::NotOwner:
        return "NOT_OWNER";
    case RejectReason::InvalidCommand:
        return "INVALID_COMMAND";
    }
    return "UNKNOWN";
}

const char* cancelReasonName(CancelReason reason) noexcept
{
    switch (reason)
    {
    case CancelReason::Requested:
        return "REQUESTED";
    case CancelReason::NoLiquidity:
        return "NO_LIQUIDITY";
    }
    return "UNKNOWN";
}

MatchingEngine::MatchingEngine(EventListener& listener, EngineConfig config)
    : m_listener(listener), m_config(config)
{
}

SequenceNumber MatchingEngine::process(const Command& command)
{
    const SequenceNumber sequence = m_nextSequence++;
    ++m_stats.commands;

    switch (command.type)
    {
    case CommandType::NewOrder:
        handleNewOrder(command, sequence);
        break;
    case CommandType::Cancel:
        handleCancel(command, sequence);
        break;
    default:
        reject(command, sequence, RejectReason::InvalidCommand);
        break;
    }

    if (m_config.checkInvariantsEveryCommand)
    {
        verify();
    }
    return sequence;
}

void MatchingEngine::reject(const Command& command, SequenceNumber sequence, RejectReason reason)
{
    ++m_stats.rejected;
    m_listener.onRejected(RejectedEvent{sequence, command.agent, command.clientOrderId, reason});
}

void MatchingEngine::handleNewOrder(const Command& command, SequenceNumber sequence)
{
    // Validate before touching any state, so a rejected command has no effect.
    if (command.orderType != OrderType::Limit && command.orderType != OrderType::Market)
    {
        reject(command, sequence, RejectReason::InvalidCommand);
        return;
    }
    if (command.side != Side::Buy && command.side != Side::Sell)
    {
        reject(command, sequence, RejectReason::InvalidCommand);
        return;
    }
    if (command.quantity <= 0)
    {
        reject(command, sequence, RejectReason::InvalidQuantity);
        return;
    }
    if (command.orderType == OrderType::Limit && command.price <= 0)
    {
        reject(command, sequence, RejectReason::InvalidPrice);
        return;
    }

    const OrderId orderId = m_nextOrderId++;
    Order taker = command.orderType == OrderType::Limit
                      ? Order::limit(orderId, command.agent, command.side, command.price, command.quantity)
                      : Order::market(orderId, command.agent, command.side, command.quantity);
    taker.setSequence(sequence);

    ++m_stats.accepted;
    m_stats.acceptedVolume += command.quantity;
    m_listener.onAccepted(AcceptedEvent{sequence, orderId, command.agent, command.clientOrderId, command.side,
                                        command.orderType, taker.price(), command.quantity});

    // Match against the opposite side, then report each execution.
    m_fillScratch.clear();
    m_book.match(taker, m_fillScratch);
    for (const Fill& fill : m_fillScratch)
    {
        const bool takerBuys = taker.side() == Side::Buy;
        const TradeEvent trade{sequence,
                               m_nextTradeId++,
                               takerBuys ? taker.id() : fill.makerOrderId,
                               takerBuys ? fill.makerOrderId : taker.id(),
                               takerBuys ? taker.agent() : fill.makerAgent,
                               takerBuys ? fill.makerAgent : taker.agent(),
                               fill.price,
                               fill.quantity,
                               taker.side()};
        ++m_stats.trades;
        m_stats.tradedVolume += fill.quantity;
        m_stats.buyFilled += fill.quantity;
        m_stats.sellFilled += fill.quantity;
        m_listener.onTrade(trade);
    }

    if (taker.isFilled())
    {
        return;
    }
    if (taker.type() == OrderType::Limit)
    {
        m_book.add(taker);
    }
    else
    {
        // Market orders never rest: the unfilled remainder is cancelled.
        ++m_stats.cancels;
        m_stats.cancelledVolume += taker.remainingQuantity();
        m_listener.onCancelled(CancelledEvent{sequence, taker.id(), taker.agent(), taker.remainingQuantity(),
                                              CancelReason::NoLiquidity});
    }
}

void MatchingEngine::handleCancel(const Command& command, SequenceNumber sequence)
{
    const Order* resting = m_book.find(command.targetOrderId);
    if (resting == nullptr)
    {
        reject(command, sequence, RejectReason::UnknownOrder);
        return;
    }
    if (resting->agent() != command.agent)
    {
        reject(command, sequence, RejectReason::NotOwner);
        return;
    }

    const std::optional<Order> removed = m_book.cancel(command.targetOrderId);
    ++m_stats.cancels;
    m_stats.cancelledVolume += removed->remainingQuantity();
    m_listener.onCancelled(CancelledEvent{sequence, removed->id(), removed->agent(),
                                          removed->remainingQuantity(), CancelReason::Requested});
}

void MatchingEngine::verify() const
{
    m_book.checkInvariants();

    if (m_stats.buyFilled != m_stats.tradedVolume || m_stats.sellFilled != m_stats.tradedVolume)
    {
        throw std::logic_error("fill volume does not match traded volume");
    }

    Quantity resting = 0;
    for (const Side side : {Side::Buy, Side::Sell})
    {
        for (const LevelSummary& level : m_book.depth(side, std::numeric_limits<std::size_t>::max()))
        {
            resting += level.quantity;
        }
    }
    const Quantity accountedFor = m_stats.buyFilled + m_stats.sellFilled + m_stats.cancelledVolume + resting;
    if (accountedFor != m_stats.acceptedVolume)
    {
        throw std::logic_error("volume not conserved: accepted " + std::to_string(m_stats.acceptedVolume) +
                               ", accounted for " + std::to_string(accountedFor));
    }
}

} // namespace agora
