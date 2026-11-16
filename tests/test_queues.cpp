// SpscRingBuffer, MutexQueue and the two CommandChannels built on them.
#include "agora/net/command_channel.hpp"
#include "agora/queue/mutex_queue.hpp"
#include "agora/queue/spsc_ring_buffer.hpp"

#include "doctest/doctest.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

using agora::MutexQueue;
using agora::SpscRingBuffer;
using namespace agora::net;

TEST_CASE("spsc: capacity rounds up to a power of two and bounds are respected")
{
    SpscRingBuffer<int> ring(5);
    CHECK(ring.capacity() == 8);
    CHECK_THROWS_AS(SpscRingBuffer<int>(0), std::invalid_argument);

    int value = 0;
    CHECK_FALSE(ring.tryPop(value));
    for (int i = 0; i < 8; ++i)
    {
        CHECK(ring.tryPush(i));
    }
    CHECK_FALSE(ring.tryPush(99)); // full
    CHECK(ring.sizeApprox() == 8);
    for (int i = 0; i < 8; ++i)
    {
        REQUIRE(ring.tryPop(value));
        CHECK(value == i); // FIFO
    }
    CHECK_FALSE(ring.tryPop(value));
}

TEST_CASE("spsc: indices wrap correctly over many laps")
{
    SpscRingBuffer<std::uint64_t> ring(4);
    std::uint64_t value = 0;
    for (std::uint64_t i = 0; i < 1000; ++i)
    {
        REQUIRE(ring.tryPush(i));
        REQUIRE(ring.tryPush(i + 1'000'000));
        REQUIRE(ring.tryPop(value));
        CHECK(value == i);
        REQUIRE(ring.tryPop(value));
        CHECK(value == i + 1'000'000);
    }
}

TEST_CASE("spsc: one producer and one consumer thread lose and reorder nothing")
{
    constexpr std::uint64_t kCount = 300'000;
    SpscRingBuffer<std::uint64_t> ring(256);

    std::thread producer(
        [&ring]
        {
            for (std::uint64_t i = 0; i < kCount; ++i)
            {
                while (!ring.tryPush(i))
                {
                    std::this_thread::yield();
                }
            }
        });

    std::uint64_t expected = 0;
    bool inOrder = true;
    while (expected < kCount)
    {
        std::uint64_t value = 0;
        if (ring.tryPop(value))
        {
            inOrder = inOrder && value == expected;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();
    CHECK(inOrder);
    CHECK(ring.sizeApprox() == 0);
}

TEST_CASE("mutex queue: bounds, FIFO order, timeout and close")
{
    MutexQueue<int> queue(2);
    CHECK_THROWS_AS(MutexQueue<int>(0), std::invalid_argument);

    int value = 0;
    CHECK_FALSE(queue.tryPop(value));
    CHECK_FALSE(queue.popWait(value, std::chrono::milliseconds(1)));
    CHECK(queue.tryPush(1));
    CHECK(queue.tryPush(2));
    CHECK_FALSE(queue.tryPush(3)); // full
    REQUIRE(queue.tryPop(value));
    CHECK(value == 1);
    REQUIRE(queue.popWait(value, std::chrono::milliseconds(1)));
    CHECK(value == 2);

    queue.close();
    CHECK_FALSE(queue.push(4));
    CHECK_FALSE(queue.tryPush(4));
}

TEST_CASE("mutex queue: several producers, nothing lost, per-producer order kept")
{
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 50'000;
    MutexQueue<int> queue(128);

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back(
            [&queue, p]
            {
                for (int i = 0; i < kPerProducer; ++i)
                {
                    queue.push(p * kPerProducer + i); // blocks while the queue is full
                }
            });
    }

    std::vector<int> next(kProducers, 0);
    bool ordered = true;
    for (int received = 0; received < kProducers * kPerProducer; ++received)
    {
        int value = 0;
        REQUIRE(queue.popWait(value, std::chrono::seconds(10)));
        const int producer = value / kPerProducer;
        ordered = ordered && (value % kPerProducer == next[static_cast<std::size_t>(producer)]++);
    }
    for (std::thread& producer : producers)
    {
        producer.join();
    }
    CHECK(ordered);
}

TEST_CASE("command channel: both kinds deliver every message, in order per session")
{
    for (const QueueKind kind : {QueueKind::Mutex, QueueKind::Spsc})
    {
        constexpr std::uint32_t kSlots = 4;
        constexpr std::uint64_t kPerSlot = 20'000;
        const std::unique_ptr<CommandChannel> channel = makeChannel(kind, kSlots, 64);
        CAPTURE(channel->name());

        std::vector<std::thread> producers;
        for (std::uint32_t slot = 0; slot < kSlots; ++slot)
        {
            producers.emplace_back(
                [&channel, slot]
                {
                    InboundMessage message;
                    message.kind = InboundKind::TickDone;
                    message.slot = slot;
                    for (std::uint64_t i = 0; i < kPerSlot; ++i)
                    {
                        message.tick = i;
                        channel->push(message);
                    }
                });
        }

        std::vector<std::uint64_t> next(kSlots, 0);
        bool ordered = true;
        for (std::uint64_t received = 0; received < kSlots * kPerSlot; ++received)
        {
            InboundMessage message;
            REQUIRE(channel->pop(message, std::chrono::seconds(10)));
            ordered = ordered && message.tick == next[message.slot]++;
        }
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        CHECK(ordered);

        InboundMessage none;
        CHECK_FALSE(channel->pop(none, std::chrono::microseconds(0)));
        CHECK_FALSE(channel->pop(none, std::chrono::microseconds(500)));

        channel->close();
        CHECK_FALSE(channel->push(none));
    }
}
