/**
 * @file protocol.cpp
 * @brief Encoding and decoding of the wire protocol.
 */
#include "agora/net/protocol.hpp"

#include <stdexcept>

namespace agora::net
{
namespace
{

/// Appends one frame to a byte buffer, field by field, little-endian.
class Writer
{
  public:
    /// Writes the header; @p payloadSize must match the fields written afterwards.
    Writer(Bytes& out, MessageType type, std::size_t payloadSize) : m_out(out)
    {
        m_out.reserve(m_out.size() + kHeaderSize + payloadSize);
        u16(static_cast<std::uint16_t>(payloadSize));
        u8(static_cast<std::uint8_t>(type));
        u8(0);
    }

    void u8(std::uint8_t value) { m_out.push_back(value); }
    void u16(std::uint16_t value) { put(value, 2); }
    void u32(std::uint32_t value) { put(value, 4); }
    void u64(std::uint64_t value) { put(value, 8); }
    void i64(std::int64_t value) { put(static_cast<std::uint64_t>(value), 8); }

    /// Writes @p count zero bytes of padding.
    void pad(std::size_t count) { m_out.insert(m_out.end(), count, static_cast<unsigned char>(0)); }

  private:
    void put(std::uint64_t value, std::size_t bytes)
    {
        for (std::size_t i = 0; i < bytes; ++i)
        {
            m_out.push_back(static_cast<unsigned char>(value >> (8 * i)));
        }
    }

    Bytes& m_out;
};

/// Reads little-endian fields from a payload. The caller checks the size first.
class Reader
{
  public:
    explicit Reader(const unsigned char* data) : m_cursor(data) {}

    std::uint8_t u8() { return *m_cursor++; }
    std::uint32_t u32() { return static_cast<std::uint32_t>(get(4)); }
    std::uint64_t u64() { return get(8); }
    std::int64_t i64() { return static_cast<std::int64_t>(get(8)); }
    void skip(std::size_t count) { m_cursor += count; }

  private:
    std::uint64_t get(std::size_t bytes)
    {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i)
        {
            value |= static_cast<std::uint64_t>(m_cursor[i]) << (8 * i);
        }
        m_cursor += bytes;
        return value;
    }

    const unsigned char* m_cursor;
};

// Payload sizes, kept next to each other so they are easy to check against
// the layouts documented in protocol.hpp.
constexpr std::size_t kHelloSize = 12;
constexpr std::size_t kNewOrderSize = 32;
constexpr std::size_t kCancelOrderSize = 16;
constexpr std::size_t kTickDoneSize = 8;
constexpr std::size_t kWelcomeSize = 8;
constexpr std::size_t kAcceptedSize = 48;
constexpr std::size_t kRejectedSize = 24;
constexpr std::size_t kCancelledSize = 32;
constexpr std::size_t kTradeSize = 64;
constexpr std::size_t kTickSize = 56;
constexpr std::size_t kEndSize = 8;

bool readSide(std::uint8_t raw, Side& side)
{
    if (raw > static_cast<std::uint8_t>(Side::Sell))
    {
        return false;
    }
    side = static_cast<Side>(raw);
    return true;
}

bool readOrderType(std::uint8_t raw, OrderType& type)
{
    if (raw > static_cast<std::uint8_t>(OrderType::Market))
    {
        return false;
    }
    type = static_cast<OrderType>(raw);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

void encode(const Hello& message, Bytes& out)
{
    Writer writer(out, MessageType::Hello, kHelloSize);
    writer.u32(message.version);
    writer.u32(message.agent);
    writer.u8(message.flags);
    writer.pad(3);
}

void encode(const NewOrder& message, Bytes& out)
{
    Writer writer(out, MessageType::NewOrder, kNewOrderSize);
    writer.u64(message.clientOrderId);
    writer.u8(static_cast<std::uint8_t>(message.side));
    writer.u8(static_cast<std::uint8_t>(message.orderType));
    writer.pad(6);
    writer.i64(message.price);
    writer.i64(message.quantity);
}

void encode(const CancelOrder& message, Bytes& out)
{
    Writer writer(out, MessageType::CancelOrder, kCancelOrderSize);
    writer.u64(message.clientOrderId);
    writer.u64(message.targetOrderId);
}

void encode(const TickDone& message, Bytes& out)
{
    Writer writer(out, MessageType::TickDone, kTickDoneSize);
    writer.u64(message.tick);
}

void encode(const Welcome& message, Bytes& out)
{
    Writer writer(out, MessageType::Welcome, kWelcomeSize);
    writer.u32(message.agent);
    writer.u8(static_cast<std::uint8_t>(message.status));
    writer.u8(message.lockstep ? 1 : 0);
    writer.pad(2);
}

void encode(const Accepted& message, Bytes& out)
{
    Writer writer(out, MessageType::Accepted, kAcceptedSize);
    writer.u64(message.sequence);
    writer.u64(message.orderId);
    writer.u64(message.clientOrderId);
    writer.u8(static_cast<std::uint8_t>(message.side));
    writer.u8(static_cast<std::uint8_t>(message.orderType));
    writer.pad(6);
    writer.i64(message.price);
    writer.i64(message.quantity);
}

void encode(const Rejected& message, Bytes& out)
{
    Writer writer(out, MessageType::Rejected, kRejectedSize);
    writer.u64(message.sequence);
    writer.u64(message.clientOrderId);
    writer.u8(static_cast<std::uint8_t>(message.reason));
    writer.pad(7);
}

void encode(const Cancelled& message, Bytes& out)
{
    Writer writer(out, MessageType::Cancelled, kCancelledSize);
    writer.u64(message.sequence);
    writer.u64(message.orderId);
    writer.i64(message.quantity);
    writer.u8(static_cast<std::uint8_t>(message.reason));
    writer.pad(7);
}

void encode(const Trade& message, Bytes& out)
{
    Writer writer(out, MessageType::Trade, kTradeSize);
    writer.u64(message.sequence);
    writer.u64(message.tradeId);
    writer.u64(message.buyOrderId);
    writer.u64(message.sellOrderId);
    writer.u32(message.buyAgent);
    writer.u32(message.sellAgent);
    writer.i64(message.price);
    writer.i64(message.quantity);
    writer.u8(static_cast<std::uint8_t>(message.aggressor));
    writer.pad(7);
}

void encode(const Tick& message, Bytes& out)
{
    Writer writer(out, MessageType::Tick, kTickSize);
    writer.u64(message.tick);
    writer.i64(message.fundamental);
    writer.i64(message.bidPrice);
    writer.i64(message.bidQuantity);
    writer.i64(message.askPrice);
    writer.i64(message.askQuantity);
    writer.i64(message.lastTradePrice);
}

void encode(const End& message, Bytes& out)
{
    Writer writer(out, MessageType::End, kEndSize);
    writer.u64(message.ticks);
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

bool decode(const Frame& frame, ClientMessage& message)
{
    Reader reader(frame.payload);
    switch (frame.type)
    {
    case MessageType::Hello:
    {
        if (frame.size != kHelloSize)
        {
            return false;
        }
        Hello hello;
        hello.version = reader.u32();
        hello.agent = reader.u32();
        hello.flags = reader.u8();
        message = hello;
        return true;
    }
    case MessageType::NewOrder:
    {
        if (frame.size != kNewOrderSize)
        {
            return false;
        }
        NewOrder order;
        order.clientOrderId = reader.u64();
        const bool sideOk = readSide(reader.u8(), order.side);
        const bool typeOk = readOrderType(reader.u8(), order.orderType);
        reader.skip(6);
        order.price = reader.i64();
        order.quantity = reader.i64();
        message = order;
        return sideOk && typeOk;
    }
    case MessageType::CancelOrder:
    {
        if (frame.size != kCancelOrderSize)
        {
            return false;
        }
        CancelOrder cancel;
        cancel.clientOrderId = reader.u64();
        cancel.targetOrderId = reader.u64();
        message = cancel;
        return true;
    }
    case MessageType::TickDone:
    {
        if (frame.size != kTickDoneSize)
        {
            return false;
        }
        TickDone done;
        done.tick = reader.u64();
        message = done;
        return true;
    }
    default:
        return false;
    }
}

bool decode(const Frame& frame, ServerMessage& message)
{
    Reader reader(frame.payload);
    switch (frame.type)
    {
    case MessageType::Welcome:
    {
        if (frame.size != kWelcomeSize)
        {
            return false;
        }
        Welcome welcome;
        welcome.agent = reader.u32();
        const std::uint8_t status = reader.u8();
        welcome.lockstep = reader.u8() != 0;
        if (status > static_cast<std::uint8_t>(WelcomeStatus::SessionClosed))
        {
            return false;
        }
        welcome.status = static_cast<WelcomeStatus>(status);
        message = welcome;
        return true;
    }
    case MessageType::Accepted:
    {
        if (frame.size != kAcceptedSize)
        {
            return false;
        }
        Accepted accepted;
        accepted.sequence = reader.u64();
        accepted.orderId = reader.u64();
        accepted.clientOrderId = reader.u64();
        const bool sideOk = readSide(reader.u8(), accepted.side);
        const bool typeOk = readOrderType(reader.u8(), accepted.orderType);
        reader.skip(6);
        accepted.price = reader.i64();
        accepted.quantity = reader.i64();
        message = accepted;
        return sideOk && typeOk;
    }
    case MessageType::Rejected:
    {
        if (frame.size != kRejectedSize)
        {
            return false;
        }
        Rejected rejected;
        rejected.sequence = reader.u64();
        rejected.clientOrderId = reader.u64();
        const std::uint8_t reason = reader.u8();
        if (reason < static_cast<std::uint8_t>(RejectReason::InvalidQuantity) ||
            reason > static_cast<std::uint8_t>(RejectReason::InvalidCommand))
        {
            return false;
        }
        rejected.reason = static_cast<RejectReason>(reason);
        message = rejected;
        return true;
    }
    case MessageType::Cancelled:
    {
        if (frame.size != kCancelledSize)
        {
            return false;
        }
        Cancelled cancelled;
        cancelled.sequence = reader.u64();
        cancelled.orderId = reader.u64();
        cancelled.quantity = reader.i64();
        const std::uint8_t reason = reader.u8();
        if (reason < static_cast<std::uint8_t>(CancelReason::Requested) ||
            reason > static_cast<std::uint8_t>(CancelReason::NoLiquidity))
        {
            return false;
        }
        cancelled.reason = static_cast<CancelReason>(reason);
        message = cancelled;
        return true;
    }
    case MessageType::Trade:
    {
        if (frame.size != kTradeSize)
        {
            return false;
        }
        Trade trade;
        trade.sequence = reader.u64();
        trade.tradeId = reader.u64();
        trade.buyOrderId = reader.u64();
        trade.sellOrderId = reader.u64();
        trade.buyAgent = reader.u32();
        trade.sellAgent = reader.u32();
        trade.price = reader.i64();
        trade.quantity = reader.i64();
        const bool sideOk = readSide(reader.u8(), trade.aggressor);
        message = trade;
        return sideOk;
    }
    case MessageType::Tick:
    {
        if (frame.size != kTickSize)
        {
            return false;
        }
        Tick tick;
        tick.tick = reader.u64();
        tick.fundamental = reader.i64();
        tick.bidPrice = reader.i64();
        tick.bidQuantity = reader.i64();
        tick.askPrice = reader.i64();
        tick.askQuantity = reader.i64();
        tick.lastTradePrice = reader.i64();
        message = tick;
        return true;
    }
    case MessageType::End:
    {
        if (frame.size != kEndSize)
        {
            return false;
        }
        End end;
        end.ticks = reader.u64();
        message = end;
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// FrameReader
// ---------------------------------------------------------------------------

void FrameReader::append(const unsigned char* data, std::size_t size)
{
    // Drop bytes already handed out so the buffer does not grow without bound.
    if (m_offset > 0)
    {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset));
        m_offset = 0;
    }
    m_buffer.insert(m_buffer.end(), data, data + size);
}

bool FrameReader::next(Frame& frame)
{
    if (pending() < kHeaderSize)
    {
        return false;
    }
    const unsigned char* header = m_buffer.data() + m_offset;
    const std::size_t payloadSize =
        static_cast<std::size_t>(header[0]) | (static_cast<std::size_t>(header[1]) << 8);
    if (payloadSize > kMaxPayload)
    {
        throw std::runtime_error("FrameReader: oversized frame");
    }
    if (pending() < kHeaderSize + payloadSize)
    {
        return false;
    }
    frame.type = static_cast<MessageType>(header[2]);
    frame.payload = header + kHeaderSize;
    frame.size = payloadSize;
    m_offset += kHeaderSize + payloadSize;
    return true;
}

} // namespace agora::net
