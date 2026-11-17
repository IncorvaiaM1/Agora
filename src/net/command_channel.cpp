/**
 * @file command_channel.cpp
 * @brief MutexChannel and SpscChannel.
 */
#include "agora/net/command_channel.hpp"

#include <thread>

namespace agora::net
{
namespace
{

/// Failed pushes before a producer facing a full ring starts sleeping instead of yielding.
constexpr int kSpinsBeforeSleep = 200;

/// Empty sweeps before the idle engine thread starts sleeping. Higher than the
/// producer's count on purpose: under steady load the next order is usually
/// less than a millisecond away, and a sleep would add its wake-up time to
/// that order's latency.
constexpr int kSweepsBeforeSleep = 2000;

/// Sleep between polls once spinning has not paid off.
constexpr std::chrono::microseconds kIdleSleep{50};

} // namespace

bool MutexChannel::pop(InboundMessage& message, std::chrono::microseconds wait)
{
    if (wait.count() <= 0)
    {
        return m_queue.tryPop(message);
    }
    return m_queue.popWait(message, wait);
}

SpscChannel::SpscChannel(std::size_t slots, std::size_t capacityPerSlot)
{
    m_rings.reserve(slots);
    for (std::size_t i = 0; i < slots; ++i)
    {
        m_rings.push_back(std::make_unique<SpscRingBuffer<InboundMessage>>(capacityPerSlot));
    }
}

bool SpscChannel::push(const InboundMessage& message)
{
    SpscRingBuffer<InboundMessage>& ring = *m_rings[message.slot];

    // Make sure the engine's sweep reaches this slot before the message lands in it.
    std::size_t seen = m_highWater.load(std::memory_order_relaxed);
    while (seen <= message.slot &&
           !m_highWater.compare_exchange_weak(seen, message.slot + 1, std::memory_order_release,
                                              std::memory_order_relaxed))
    {
    }

    int spins = 0;
    for (;;)
    {
        if (m_closed.load(std::memory_order_acquire))
        {
            return false;
        }
        if (ring.tryPush(message))
        {
            return true;
        }
        // Ring full: the engine is behind. Yield first, then back off.
        if (++spins < kSpinsBeforeSleep)
        {
            std::this_thread::yield();
        }
        else
        {
            std::this_thread::sleep_for(kIdleSleep);
        }
    }
}

bool SpscChannel::sweep(InboundMessage& message) noexcept
{
    const std::size_t count = m_highWater.load(std::memory_order_acquire);
    if (count == 0)
    {
        return false;
    }
    if (m_cursor >= count)
    {
        m_cursor = 0;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t index = (m_cursor + i) % count;
        if (m_rings[index]->tryPop(message))
        {
            // Start the next sweep after this ring so a busy client cannot starve the others.
            m_cursor = (index + 1) % count;
            return true;
        }
    }
    return false;
}

bool SpscChannel::pop(InboundMessage& message, std::chrono::microseconds wait)
{
    if (sweep(message))
    {
        return true;
    }
    if (wait.count() <= 0)
    {
        return false;
    }

    // Nothing ready. Spin briefly (cheap, lowest latency), then sleep in short
    // steps so an idle exchange does not burn a whole core.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    int spins = 0;
    for (;;)
    {
        if (sweep(message))
        {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        if (++spins < kSweepsBeforeSleep)
        {
            std::this_thread::yield();
        }
        else
        {
            std::this_thread::sleep_for(kIdleSleep);
        }
    }
}

std::unique_ptr<CommandChannel> makeChannel(QueueKind kind, std::size_t slots, std::size_t capacityPerSlot)
{
    if (kind == QueueKind::Mutex)
    {
        // Same total capacity as the per-session rings, so the comparison is fair.
        return std::make_unique<MutexChannel>(slots * capacityPerSlot);
    }
    return std::make_unique<SpscChannel>(slots, capacityPerSlot);
}

} // namespace agora::net
