/**
 * @file types.hpp
 * @brief Fundamental scalar types shared by every Agora component.
 *
 * Prices are stored as integer ticks, never as floating point, so that
 * order book comparisons are exact and trade logs are byte-for-byte
 * reproducible across runs and machines.
 */
#pragma once

#include <cstdint>

namespace agora
{

/// Price in integer ticks (e.g. 1 tick = $0.01).
using Price = std::int64_t;

/// Order or trade quantity in shares.
using Quantity = std::int64_t;

/// Exchange-assigned order identifier, unique for the life of a session.
using OrderId = std::uint64_t;

/// Identifier of the trading agent (client connection) that owns an order.
using AgentId = std::uint32_t;

/// Monotonic sequence number assigned by the matching engine.
/// Defines the total order of events and therefore price-time priority.
using SequenceNumber = std::uint64_t;

/// Side of the book an order rests on.
enum class Side : std::uint8_t
{
    Buy = 0,
    Sell = 1
};

/// Order type accepted by the matching engine.
enum class OrderType : std::uint8_t
{
    Limit = 0, ///< Rests on the book at its limit price if not fully filled.
    Market = 1 ///< Takes liquidity until filled or the opposite side is empty.
};

/// Returns the opposite side.
constexpr Side opposite(Side side) noexcept
{
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

/// Returns a short printable name for a side ("BUY" / "SELL").
const char* sideName(Side side) noexcept;

/// Returns a short printable name for an order type ("LIMIT" / "MARKET").
const char* orderTypeName(OrderType type) noexcept;

} // namespace agora
