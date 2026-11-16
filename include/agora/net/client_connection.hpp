/**
 * @file client_connection.hpp
 * @brief ClientConnection: the client end of the wire protocol.
 */
#pragma once

#include "agora/net/protocol.hpp"
#include "agora/net/socket.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace agora::net
{

/**
 * @brief A blocking connection from an agent (or any other client) to the exchange.
 *
 * Outgoing messages are buffered and go out together on flush(), so an agent
 * that sends several orders in one tick costs one system call. receive()
 * blocks until one whole message has arrived. Not thread-safe as a whole,
 * but one thread may send while another receives.
 */
class ClientConnection
{
  public:
    /// Connects to the exchange. @throws std::runtime_error if it cannot.
    ClientConnection(const std::string& host, std::uint16_t port);

    /// Buffers one message for the exchange.
    template <typename Message> void send(const Message& message) { encode(message, m_outbound); }

    /// Writes all buffered messages. @return false if the connection is broken.
    bool flush();

    /**
     * @brief Blocks until the next message from the exchange.
     * @return false when the exchange has closed the connection.
     * @throws std::runtime_error on a malformed message.
     */
    bool receive(ServerMessage& message);

    /// Closes the connection (also wakes a thread blocked in receive()).
    void close() noexcept { m_socket.shutdownBoth(); }

  private:
    Socket m_socket;
    Bytes m_outbound;
    FrameReader m_frames;
    std::array<unsigned char, 16 * 1024> m_readBuffer{};
};

} // namespace agora::net
