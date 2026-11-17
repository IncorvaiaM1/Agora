/**
 * @file command_channel.hpp
 * @brief CommandChannel: carries messages from the session threads to the engine thread.
 *
 * Two implementations sit behind one interface so the exchange can switch
 * between them with a flag and everything else stays the same (result 2):
 *
 *  - MutexChannel: one shared MutexQueue; every session thread locks it.
 *  - SpscChannel:  one lock-free SpscRingBuffer per session; the engine
 *                  thread polls them round-robin.
 */
#pragma once

#include "agora/core/command.hpp"
#include "agora/queue/mutex_queue.hpp"
#include "agora/queue/spsc_ring_buffer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace agora::net
{

/// What a session thread is telling the engine thread.
enum class InboundKind : std::uint8_t
{
    Connect = 1,    ///< Handshake received; `agent` and `flags` come from Hello.
    Disconnect = 2, ///< Connection closed or broke protocol. Always the slot's last message.
    Order = 3,      ///< `command` holds a new order or a cancel.
    TickDone = 4    ///< The client finished tick `tick` (lockstep mode).
};

/// One message from a session thread. Plain data so it can be copied through either queue.
struct InboundMessage
{
    InboundKind kind = InboundKind::Order;
    std::uint8_t flags = 0;         ///< Hello flags (Connect only).
    std::uint32_t slot = 0;         ///< Gateway slot of the sending session.
    AgentId agent = 0;              ///< Requested agent id (Connect only).
    std::uint32_t version = 0;      ///< Protocol version (Connect only).
    std::uint64_t tick = 0;         ///< Tick number (TickDone only).
    Command command;                ///< The request (Order only). `command.agent` is set by the engine.
};

/// Which CommandChannel implementation the exchange uses.
enum class QueueKind
{
    Mutex,
    Spsc
};

/// Interface between N producer threads (one per session slot) and one consumer thread.
class CommandChannel
{
  public:
    virtual ~CommandChannel() = default;

    /**
     * @brief Enqueues a message from the session that owns @p message.slot.
     *
     * Blocks while the queue is full, which pushes back on a client that
     * sends faster than the engine can match.
     *
     * @return false if the channel was closed.
     */
    virtual bool push(const InboundMessage& message) = 0;

    /**
     * @brief Dequeues the next message, waiting up to @p wait if none is ready.
     * @return false on timeout. Only the engine thread may call this.
     */
    virtual bool pop(InboundMessage& message, std::chrono::microseconds wait) = 0;

    /// Unblocks producers; later pushes return false.
    virtual void close() = 0;

    /// "mutex" or "spsc", for logs and result files.
    virtual const char* name() const noexcept = 0;
};

/// All sessions share one mutex-protected queue.
class MutexChannel final : public CommandChannel
{
  public:
    explicit MutexChannel(std::size_t capacity) : m_queue(capacity) {}

    bool push(const InboundMessage& message) override { return m_queue.push(message); }
    bool pop(InboundMessage& message, std::chrono::microseconds wait) override;
    void close() override { m_queue.close(); }
    const char* name() const noexcept override { return "mutex"; }

  private:
    MutexQueue<InboundMessage> m_queue;
};

/// One lock-free ring per session slot, polled by the engine thread.
class SpscChannel final : public CommandChannel
{
  public:
    /**
     * @param slots Number of session slots (one ring each).
     * @param capacityPerSlot Ring size per session.
     */
    SpscChannel(std::size_t slots, std::size_t capacityPerSlot);

    bool push(const InboundMessage& message) override;
    bool pop(InboundMessage& message, std::chrono::microseconds wait) override;
    void close() override { m_closed.store(true, std::memory_order_release); }
    const char* name() const noexcept override { return "spsc"; }

  private:
    /// One pass over every ring, starting after the last one served.
    bool sweep(InboundMessage& message) noexcept;

    std::vector<std::unique_ptr<SpscRingBuffer<InboundMessage>>> m_rings;
    std::size_t m_cursor = 0; ///< Engine thread only: where the next sweep starts (fairness).
    /// One past the highest slot that has ever pushed. The gateway hands out the
    /// lowest free slot first, so sweeping only this far skips the rings that
    /// have never had a client instead of polling all of them on every pop.
    std::atomic<std::size_t> m_highWater{0};
    std::atomic<bool> m_closed{false};
};

/// Builds the channel selected by @p kind.
std::unique_ptr<CommandChannel> makeChannel(QueueKind kind, std::size_t slots, std::size_t capacityPerSlot);

} // namespace agora::net
