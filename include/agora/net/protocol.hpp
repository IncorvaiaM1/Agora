/**
 * @file protocol.hpp
 * @brief Binary wire protocol between the exchange and its clients.
 *
 * Every message is one frame:
 *
 *   header  : payload length u16 | message type u8 | reserved u8   (4 bytes)
 *   payload : fixed layout per message type (see each struct below)
 *
 * All integers are little-endian regardless of host byte order, and fields
 * are written one by one, never by copying structs, so the format has no
 * padding or alignment surprises. Payloads are fixed size; a frame whose
 * length does not match its type is a protocol error.
 *
 * Client to exchange: Hello, NewOrder, CancelOrder, TickDone.
 * Exchange to client: Welcome, Accepted, Rejected, Cancelled, Trade, Tick, End.
 */
#pragma once

#include "agora/core/events.hpp"
#include "agora/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace agora::net
{

/// Version sent in Hello; the exchange refuses clients that disagree.
constexpr std::uint32_t kProtocolVersion = 1;

/// Size of the frame header in bytes.
constexpr std::size_t kHeaderSize = 4;

/// Largest payload of any message (Trade). Longer frames are rejected.
constexpr std::size_t kMaxPayload = 64;

/// Growable byte buffer used for encoding.
using Bytes = std::vector<unsigned char>;

/// Frame type tag.
enum class MessageType : std::uint8_t
{
    Hello = 1,
    NewOrder = 2,
    CancelOrder = 3,
    TickDone = 4,
    Welcome = 16,
    Accepted = 17,
    Rejected = 18,
    Cancelled = 19,
    Trade = 20,
    Tick = 21,
    End = 22
};

/// Bit flags carried in Hello.
enum HelloFlags : std::uint8_t
{
    kFlagMarketData = 1, ///< Send me every trade, not only my own.
    kFlagObserver = 2    ///< I never trade (viewer, risk engine); do not wait for me in lockstep.
};

/// Result of the handshake, carried in Welcome.
enum class WelcomeStatus : std::uint8_t
{
    Ok = 0,
    BadVersion = 1,
    DuplicateAgent = 2,
    SessionClosed = 3 ///< A lockstep session is already running; late agents would break determinism.
};

// ---------------------------------------------------------------------------
// Client to exchange
// ---------------------------------------------------------------------------

/// First message on every connection. Payload: version u32 | agent u32 | flags u8 | pad 3.
struct Hello
{
    std::uint32_t version = kProtocolVersion;
    AgentId agent = 0; ///< Chosen by the client so runs are reproducible; must be unique.
    std::uint8_t flags = 0;
};

/// Payload: clientOrderId u64 | side u8 | orderType u8 | pad 6 | price i64 | quantity i64.
struct NewOrder
{
    ClientOrderId clientOrderId = 0;
    Side side = Side::Buy;
    OrderType orderType = OrderType::Limit;
    Price price = 0;
    Quantity quantity = 0;
};

/// Payload: clientOrderId u64 | targetOrderId u64.
struct CancelOrder
{
    ClientOrderId clientOrderId = 0;
    OrderId targetOrderId = 0;
};

/// "I have sent everything I want to do in this tick." Payload: tick u64.
struct TickDone
{
    std::uint64_t tick = 0;
};

// ---------------------------------------------------------------------------
// Exchange to client
// ---------------------------------------------------------------------------

/// Payload: agent u32 | status u8 | lockstep u8 | pad 2.
struct Welcome
{
    AgentId agent = 0;
    WelcomeStatus status = WelcomeStatus::Ok;
    bool lockstep = false;
};

/// Payload: sequence u64 | orderId u64 | clientOrderId u64 | side u8 | orderType u8 | pad 6 |
///          price i64 | quantity i64.
struct Accepted
{
    SequenceNumber sequence = 0;
    OrderId orderId = 0;
    ClientOrderId clientOrderId = 0;
    Side side = Side::Buy;
    OrderType orderType = OrderType::Limit;
    Price price = 0;
    Quantity quantity = 0;
};

/// Payload: sequence u64 | clientOrderId u64 | reason u8 | pad 7.
struct Rejected
{
    SequenceNumber sequence = 0;
    ClientOrderId clientOrderId = 0;
    RejectReason reason = RejectReason::InvalidCommand;
};

/// Payload: sequence u64 | orderId u64 | quantity i64 | reason u8 | pad 7.
struct Cancelled
{
    SequenceNumber sequence = 0;
    OrderId orderId = 0;
    Quantity quantity = 0;
    CancelReason reason = CancelReason::Requested;
};

/// Payload: sequence u64 | tradeId u64 | buyOrderId u64 | sellOrderId u64 | buyAgent u32 |
///          sellAgent u32 | price i64 | quantity i64 | aggressor u8 | pad 7.
struct Trade
{
    SequenceNumber sequence = 0;
    TradeId tradeId = 0;
    OrderId buyOrderId = 0;
    OrderId sellOrderId = 0;
    AgentId buyAgent = 0;
    AgentId sellAgent = 0;
    Price price = 0;
    Quantity quantity = 0;
    Side aggressor = Side::Buy;
};

/**
 * @brief Start of a simulation tick, with a top-of-book snapshot.
 *
 * A price of 0 means "none" (empty side, or no trade yet).
 * Payload: tick u64 | fundamental i64 | bidPrice i64 | bidQuantity i64 | askPrice i64 |
 *          askQuantity i64 | lastTradePrice i64.
 */
struct Tick
{
    std::uint64_t tick = 0;
    Price fundamental = 0; ///< Exchange-generated fundamental value (seeded random walk).
    Price bidPrice = 0;
    Quantity bidQuantity = 0;
    Price askPrice = 0;
    Quantity askQuantity = 0;
    Price lastTradePrice = 0;
};

/// The session is over; the exchange closes the connection next. Payload: ticks u64.
struct End
{
    std::uint64_t ticks = 0;
};

/// Any message a client can send.
using ClientMessage = std::variant<Hello, NewOrder, CancelOrder, TickDone>;

/// Any message the exchange can send.
using ServerMessage = std::variant<Welcome, Accepted, Rejected, Cancelled, Trade, Tick, End>;

// ---------------------------------------------------------------------------
// Encoding: each call appends one complete frame (header + payload) to @p out.
// ---------------------------------------------------------------------------
void encode(const Hello& message, Bytes& out);
void encode(const NewOrder& message, Bytes& out);
void encode(const CancelOrder& message, Bytes& out);
void encode(const TickDone& message, Bytes& out);
void encode(const Welcome& message, Bytes& out);
void encode(const Accepted& message, Bytes& out);
void encode(const Rejected& message, Bytes& out);
void encode(const Cancelled& message, Bytes& out);
void encode(const Trade& message, Bytes& out);
void encode(const Tick& message, Bytes& out);
void encode(const End& message, Bytes& out);

/// One frame located inside a FrameReader's buffer.
struct Frame
{
    MessageType type = MessageType::Hello;
    const unsigned char* payload = nullptr;
    std::size_t size = 0;
};

/**
 * @brief Decodes a client-to-exchange frame.
 * @return false if the type is not a client message, the length is wrong, or
 *         an enum field holds an unknown value.
 */
bool decode(const Frame& frame, ClientMessage& message);

/// Decodes an exchange-to-client frame. Same failure rules as the client overload.
bool decode(const Frame& frame, ServerMessage& message);

/**
 * @brief Reassembles frames from a TCP byte stream.
 *
 * TCP delivers bytes, not messages: one recv() can return half a frame or
 * several frames. Feed whatever arrives to append(), then call next() until
 * it returns false.
 */
class FrameReader
{
  public:
    /// Adds received bytes to the internal buffer.
    void append(const unsigned char* data, std::size_t size);

    /**
     * @brief Extracts the next complete frame, if one is buffered.
     *
     * The returned Frame points into the internal buffer and stays valid
     * until the next call to append() or next().
     *
     * @throws std::runtime_error if the header announces a payload longer
     *         than kMaxPayload (the stream cannot be resynchronized).
     */
    bool next(Frame& frame);

    /// Bytes buffered but not yet returned as frames.
    std::size_t pending() const noexcept { return m_buffer.size() - m_offset; }

  private:
    Bytes m_buffer;
    std::size_t m_offset = 0;
};

} // namespace agora::net
