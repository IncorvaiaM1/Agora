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

/// Empty polls before a waiting thread starts sleeping instead of spinning.
constexpr int kSpinsBeforeSleep = 200;

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
    const std::size_t count = m_rings.size();
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
