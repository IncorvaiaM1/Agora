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

// How the idle engine thread waits on the SPSC rings, in three stages:
//
//  1. Busy polls: keep the core and poll again after a CPU pause. Under load
//     the next order is microseconds away, so this is where the engine waits
//     almost all of the time. It must not call yield() here: on a busy node
//     yield() hands the core to whichever thread is runnable, and the engine,
//     the one thread everything else is waiting for, then queues behind it.
//     The first PACE-ICE runs showed exactly that as runs that were randomly
//     20 to 40 percent slower.
//  2. Yield polls: nothing for a while, so let other threads run between polls.
//  3. Sleep: the exchange is idle; stop burning a core.
constexpr int kBusyPolls = 4000;
constexpr int kYieldPolls = 2000;

/// The clock is read only once per this many busy polls; reading it costs more than a poll.
constexpr int kPollsPerClockCheck = 64;

/// Sleep between polls once spinning has not paid off.
constexpr std::chrono::microseconds kIdleSleep{50};

/// Tells the CPU this is a spin-wait loop: saves power and lets a sibling
/// hyperthread run, without giving the core back to the scheduler.
inline void cpuRelax() noexcept
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

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

    // Nothing ready: wait in the three stages described at the top of this file.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    int polls = 0;
    for (;;)
    {
        if (sweep(message))
        {
            return true;
        }
        ++polls;
        if (polls < kBusyPolls)
        {
            cpuRelax();
            if (polls % kPollsPerClockCheck != 0)
            {
                continue;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        if (polls >= kBusyPolls + kYieldPolls)
        {
            std::this_thread::sleep_for(kIdleSleep);
        }
        else if (polls >= kBusyPolls)
        {
            std::this_thread::yield();
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
