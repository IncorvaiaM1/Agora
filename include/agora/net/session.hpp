/**
 * @file session.hpp
 * @brief Session: one client connection as seen by the exchange.
 */
#pragma once

#include "agora/net/protocol.hpp"
#include "agora/net/socket.hpp"

#include <atomic>
#include <cstdint>

namespace agora::net
{

/**
 * @brief One connected client: its socket, identity and outbound buffer.
 *
 * Two threads touch a Session, each with its own half:
 *  - the session's reader thread (owned by the Gateway) only calls
 *    socket().receive();
 *  - the engine thread owns everything else: identity, the outbound buffer,
 *    and all sends.
 *
 * Replies are batched: the engine thread queues any number of messages with
 * send() and then calls flush() once, which turns many small writes into one
 * system call per client per batch.
 */
class Session
{
  public:
    Session(std::uint32_t slot, Socket socket);

    std::uint32_t slot() const noexcept { return m_slot; }
    const Socket& socket() const noexcept { return m_socket; }

    // --- engine thread only ------------------------------------------------

    /// Records the identity announced in Hello once the engine accepts it.
    void registerAs(AgentId agent, std::uint8_t flags) noexcept;

    bool registered() const noexcept { return m_registered; }
    AgentId agent() const noexcept { return m_agent; }
    bool wantsMarketData() const noexcept { return (m_flags & kFlagMarketData) != 0; }
    bool isObserver() const noexcept { return (m_flags & kFlagObserver) != 0; }

    /// Encodes @p message into the outbound buffer. Nothing is sent until flush().
    template <typename Message> void send(const Message& message)
    {
        if (alive())
        {
            encode(message, m_outbound);
        }
    }

    /// True if messages are waiting for flush().
    bool hasPending() const noexcept { return !m_outbound.empty(); }

    /**
     * @brief Writes the outbound buffer to the socket.
     *
     * If the client is gone or too slow (send timeout), the session is marked
     * dead and its socket shut down, which makes the reader thread see end of
     * stream and report the disconnect through the normal path.
     */
    void flush() noexcept;

    /// Stops all traffic and wakes the reader thread.
    void kill() noexcept;

    // --- any thread ----------------------------------------------------------

    bool alive() const noexcept { return m_alive.load(std::memory_order_acquire); }

  private:
    const std::uint32_t m_slot;
    Socket m_socket;
    Bytes m_outbound;
    AgentId m_agent = 0;
    std::uint8_t m_flags = 0;
    bool m_registered = false;
    std::atomic<bool> m_alive{true};
};

} // namespace agora::net
