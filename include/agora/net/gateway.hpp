/**
 * @file gateway.hpp
 * @brief Gateway: accepts TCP clients and runs one reader thread per client.
 */
#pragma once

#include "agora/net/command_channel.hpp"
#include "agora/net/session.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace agora::net
{

/// Gateway settings.
struct GatewayConfig
{
    std::string bindHost = "127.0.0.1"; ///< Use "0.0.0.0" to accept clients from other nodes.
    std::uint16_t port = 9000;          ///< 0 lets the OS choose (see Gateway::port()).
    std::size_t maxSessions = 128;      ///< Connections beyond this are refused.
};

/**
 * @brief The network front of the exchange.
 *
 * One acceptor thread waits for connections. Each accepted client gets a
 * slot, a Session and a dedicated reader thread that decodes frames and
 * pushes them into the CommandChannel. The engine thread on the other side
 * of the channel never blocks on a socket read.
 *
 * Slot life cycle: Free -> Active (acceptor) -> Finished (engine, after it
 * handled the slot's Disconnect) -> Active again (acceptor, after joining the
 * old reader thread). Because the engine releases a slot only after the
 * Disconnect, which is the last message its reader ever pushes, a recycled
 * slot can never receive stale messages.
 */
class Gateway
{
  public:
    Gateway(CommandChannel& channel, GatewayConfig config);
    ~Gateway();

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    /// Binds the listening socket and starts the acceptor thread. @throws std::runtime_error.
    void start();

    /// Stops accepting, disconnects every client and joins all threads. Safe to call twice.
    void stop();

    /// The bound port (useful when the configured port was 0).
    std::uint16_t port() const noexcept { return m_port; }

    std::size_t maxSessions() const noexcept { return m_slots.size(); }

    /// Session in @p slot. Engine thread only, between that slot's Connect and its release().
    Session& session(std::uint32_t slot) { return *m_slots[slot].session; }

    /// Engine thread: the slot's Disconnect has been handled; the acceptor may reuse it.
    void release(std::uint32_t slot) noexcept;

  private:
    enum class SlotState : int
    {
        Free,
        Active,
        Finished
    };

    struct Slot
    {
        std::unique_ptr<Session> session;
        std::thread reader;
        std::atomic<SlotState> state{SlotState::Free};
    };

    void acceptLoop();
    void readLoop(std::uint32_t slot);

    /// Finds a reusable slot, joining its previous reader thread. @return false if all are busy.
    bool claimSlot(std::uint32_t& slot);

    CommandChannel& m_channel;
    GatewayConfig m_config;
    Socket m_listener;
    std::uint16_t m_port = 0;
    std::vector<Slot> m_slots;
    std::thread m_acceptor;
    std::atomic<bool> m_stopping{false};
    bool m_started = false;
};

} // namespace agora::net
