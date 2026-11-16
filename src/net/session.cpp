/**
 * @file session.cpp
 * @brief Implementation of Session.
 */
#include "agora/net/session.hpp"

#include <utility>

namespace agora::net
{
namespace
{

/// A client that cannot take data for this long is dropped rather than stalling the engine.
constexpr int kSendTimeoutMillis = 2000;

} // namespace

Session::Session(std::uint32_t slot, Socket socket) : m_slot(slot), m_socket(std::move(socket))
{
    m_socket.setNoDelay();
    m_socket.setSendTimeout(kSendTimeoutMillis);
}

void Session::registerAs(AgentId agent, std::uint8_t flags) noexcept
{
    m_agent = agent;
    m_flags = flags;
    m_registered = true;
}

void Session::flush() noexcept
{
    if (m_outbound.empty())
    {
        return;
    }
    if (alive() && !m_socket.sendAll(m_outbound.data(), m_outbound.size()))
    {
        kill();
    }
    m_outbound.clear();
}

void Session::kill() noexcept
{
    m_alive.store(false, std::memory_order_release);
    m_socket.shutdownBoth();
}

} // namespace agora::net
