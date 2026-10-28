/**
 * @file matching_engine.hpp
 * @brief MatchingEngine: sequences commands, matches orders, emits events.
 */
#pragma once

#include "agora/core/command.hpp"
#include "agora/core/events.hpp"
#include "agora/core/order_book.hpp"

#include <vector>

namespace agora
{

/// Engine options.
struct EngineConfig
{
    /// Run OrderBook::checkInvariants() and the volume checks after every
    /// command. O(book size) per command, so for tests and debugging only.
    bool checkInvariantsEveryCommand = false;
};

/// Running counters kept by the engine. Used for the conservation checks
/// (result 6) and for benchmark reporting.
struct EngineStats
{
    std::uint64_t commands = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t cancels = 0; ///< Cancelled events emitted (requested + no liquidity).
    std::uint64_t trades = 0;
    Quantity tradedVolume = 0;    ///< Sum of trade quantities.
    Quantity buyFilled = 0;       ///< Sum of fills credited to buy orders.
    Quantity sellFilled = 0;      ///< Sum of fills credited to sell orders.
    Quantity acceptedVolume = 0;  ///< Sum of quantities of accepted orders.
    Quantity cancelledVolume = 0; ///< Sum of quantities removed by cancels.
};

/**
 * @brief Single-threaded matching engine for one instrument.
 *
 * The engine is a deterministic state machine: given the same sequence of
 * Commands it produces the same events, bit for bit. It owns the OrderBook,
 * assigns sequence numbers, order ids and trade ids, validates input, and
 * reports every outcome to its EventListener. It performs no I/O and holds
 * no locks; the gateway is responsible for feeding it from one thread.
 *
 * Matching rules:
 *  - Price-time priority; trades execute at the resting (maker) price.
 *  - A limit order trades as far as it crosses, then rests the remainder.
 *  - A market order trades until filled or the opposite side is empty; any
 *    remainder is cancelled with CancelReason::NoLiquidity. It never rests.
 *  - Only the agent that owns a resting order may cancel it.
 */
class MatchingEngine
{
  public:
    explicit MatchingEngine(EventListener& listener, EngineConfig config = {});

    /**
     * @brief Processes one command and emits its events.
     * @return The sequence number assigned to the command.
     */
    SequenceNumber process(const Command& command);

    const OrderBook& book() const noexcept { return m_book; }
    const EngineStats& stats() const noexcept { return m_stats; }

    /// Sequence number that the next command will receive.
    SequenceNumber nextSequence() const noexcept { return m_nextSequence; }

    /**
     * @brief Checks the book invariants plus volume conservation.
     *
     * Conservation: every trade fills one buy and one sell, so
     *   buyFilled == sellFilled == tradedVolume,
     * and every accepted share ends up filled, cancelled or still resting:
     *   acceptedVolume == buyFilled + sellFilled + cancelledVolume + restingVolume.
     *
     * @throws std::logic_error on the first violation.
     */
    void verify() const;

  private:
    void handleNewOrder(const Command& command, SequenceNumber sequence);
    void handleCancel(const Command& command, SequenceNumber sequence);
    void reject(const Command& command, SequenceNumber sequence, RejectReason reason);

    EventListener& m_listener;
    EngineConfig m_config;
    OrderBook m_book;
    EngineStats m_stats;
    SequenceNumber m_nextSequence = 1;
    OrderId m_nextOrderId = 1;
    TradeId m_nextTradeId = 1;
    std::vector<Fill> m_fillScratch; ///< Reused across commands to avoid allocation.
};

} // namespace agora
