/**
 * @file spsc_ring_buffer.hpp
 * @brief SpscRingBuffer: bounded lock-free queue for one producer and one consumer.
 *
 * Each client session owns one of these between its reader thread (the only
 * producer) and the engine thread (the only consumer). It is compared against
 * MutexQueue in result 2.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace agora
{

/**
 * @brief Wait-free single-producer single-consumer ring buffer.
 *
 * Exactly one thread may call tryPush() and exactly one (other) thread may
 * call tryPop(). Capacity is rounded up to a power of two so the index wrap is
 * a bit mask. The head and tail indices live on separate cache lines so the
 * two threads do not false-share, and each side keeps a cached copy of the
 * other side's index so the common case touches no shared cache line at all.
 *
 * @tparam T Element type; must be default constructible and copy assignable.
 */
template <typename T> class SpscRingBuffer
{
  public:
    /// @param capacity Minimum number of elements the buffer can hold (> 0).
    explicit SpscRingBuffer(std::size_t capacity)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("SpscRingBuffer: capacity must be positive");
        }
        std::size_t rounded = 1;
        while (rounded < capacity)
        {
            rounded <<= 1;
        }
        m_mask = rounded - 1;
        m_slots = std::make_unique<T[]>(rounded);
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    /// Producer side. @return false if the buffer is full.
    bool tryPush(const T& value) noexcept
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_cachedHead > m_mask)
        {
            // Looks full; refresh our view of the consumer's progress.
            m_cachedHead = m_head.load(std::memory_order_acquire);
            if (tail - m_cachedHead > m_mask)
            {
                return false;
            }
        }
        m_slots[tail & m_mask] = value;
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Consumer side. @return false if the buffer is empty.
    bool tryPop(T& value) noexcept
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_cachedTail)
        {
            // Looks empty; refresh our view of the producer's progress.
            m_cachedTail = m_tail.load(std::memory_order_acquire);
            if (head == m_cachedTail)
            {
                return false;
            }
        }
        value = m_slots[head & m_mask];
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Number of elements the buffer can hold.
    std::size_t capacity() const noexcept { return m_mask + 1; }

    /// Approximate element count; exact only when both threads are idle.
    std::size_t sizeApprox() const noexcept
    {
        return m_tail.load(std::memory_order_acquire) - m_head.load(std::memory_order_acquire);
    }

  private:
    static constexpr std::size_t kCacheLine = 64;

    std::unique_ptr<T[]> m_slots;
    std::size_t m_mask = 0;

    alignas(kCacheLine) std::atomic<std::size_t> m_head{0}; ///< Next slot to read (consumer writes).
    std::size_t m_cachedTail = 0;                           ///< Consumer's last view of m_tail.

    alignas(kCacheLine) std::atomic<std::size_t> m_tail{0}; ///< Next slot to write (producer writes).
    std::size_t m_cachedHead = 0;                           ///< Producer's last view of m_head.
};

} // namespace agora
