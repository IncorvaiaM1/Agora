/**
 * @file mutex_queue.hpp
 * @brief MutexQueue: bounded multi-producer queue guarded by a mutex.
 *
 * The baseline for result 2. All session threads push into one shared queue
 * and the engine thread pops from it. It offers the same tryPush() / tryPop()
 * pair as SpscRingBuffer, plus blocking variants built on condition variables.
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>

namespace agora
{

/**
 * @brief Thread-safe bounded FIFO queue. Any number of producers and consumers.
 * @tparam T Element type; must be copyable.
 */
template <typename T> class MutexQueue
{
  public:
    /// @param capacity Maximum number of queued elements (> 0).
    explicit MutexQueue(std::size_t capacity) : m_capacity(capacity)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("MutexQueue: capacity must be positive");
        }
    }

    MutexQueue(const MutexQueue&) = delete;
    MutexQueue& operator=(const MutexQueue&) = delete;

    /// @return false if the queue is full or closed.
    bool tryPush(const T& value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed || m_items.size() >= m_capacity)
            {
                return false;
            }
            m_items.push_back(value);
        }
        m_notEmpty.notify_one();
        return true;
    }

    /// Blocks while the queue is full. @return false if the queue was closed.
    bool push(const T& value)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_notFull.wait(lock, [this] { return m_closed || m_items.size() < m_capacity; });
            if (m_closed)
            {
                return false;
            }
            m_items.push_back(value);
        }
        m_notEmpty.notify_one();
        return true;
    }

    /// @return false if the queue is empty.
    bool tryPop(T& value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_items.empty())
            {
                return false;
            }
            value = m_items.front();
            m_items.pop_front();
        }
        m_notFull.notify_one();
        return true;
    }

    /// Waits up to @p timeout for an element. @return false on timeout.
    template <typename Rep, typename Period> bool popWait(T& value, std::chrono::duration<Rep, Period> timeout)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_notEmpty.wait_for(lock, timeout, [this] { return !m_items.empty(); }))
            {
                return false;
            }
            value = m_items.front();
            m_items.pop_front();
        }
        m_notFull.notify_one();
        return true;
    }

    /// Wakes every blocked producer; later pushes fail. Queued items can still be popped.
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_notFull.notify_all();
        m_notEmpty.notify_all();
    }

    std::size_t capacity() const noexcept { return m_capacity; }

    std::size_t sizeApprox() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

  private:
    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<T> m_items;
    bool m_closed = false;
};

} // namespace agora
