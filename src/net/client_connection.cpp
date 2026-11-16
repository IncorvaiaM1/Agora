/**
 * @file client_connection.cpp
 * @brief Implementation of ClientConnection.
 */
#include "agora/net/client_connection.hpp"

#include <stdexcept>

namespace agora::net
{

ClientConnection::ClientConnection(const std::string& host, std::uint16_t port)
    : m_socket(Socket::connectTo(host, port))
{
}

bool ClientConnection::flush()
{
    if (m_outbound.empty())
    {
        return true;
    }
    const bool ok = m_socket.sendAll(m_outbound.data(), m_outbound.size());
    m_outbound.clear();
    return ok;
}

bool ClientConnection::receive(ServerMessage& message)
{
    Frame frame;
    while (!m_frames.next(frame))
    {
        const long received = m_socket.receive(m_readBuffer.data(), m_readBuffer.size());
        if (received <= 0)
        {
            return false;
        }
        m_frames.append(m_readBuffer.data(), static_cast<std::size_t>(received));
    }
    if (!decode(frame, message))
    {
        throw std::runtime_error("ClientConnection: malformed message from exchange");
    }
    return true;
}

} // namespace agora::net
