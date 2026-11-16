/**
 * @file gateway.cpp
 * @brief Implementation of Gateway: acceptor thread and per-client reader threads.
 */
#include "agora/net/gateway.hpp"

#include <array>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace agora::net
{
namespace
{

/// How often the acceptor wakes up to check for shutdown.
constexpr int kAcceptPollMillis = 100;

/// Bytes read from a socket per recv() call.
constexpr std::size_t kReadBufferSize = 16 * 1024;

} // namespace

Gateway::Gateway(CommandChannel& channel, GatewayConfig config)
    : m_channel(channel), m_config(std::move(config)), m_slots(m_config.maxSessions)
{
}

Gateway::~Gateway()
{
    stop();
}

void Gateway::start()
{
    m_listener = Socket::listenOn(m_config.bindHost, m_config.port);
    m_port = m_listener.localPort();
    m_started = true;
    m_acceptor = std::thread(&Gateway::acceptLoop, this);
}

void Gateway::stop()
{
    if (!m_started)
    {
        return;
    }
    m_started = false;

    m_stopping.store(true, std::memory_order_release);
    if (m_acceptor.joinable())
    {
        m_acceptor.join();
    }
    m_listener.close();

    // Readers may be blocked in recv() or in a push to a full channel; wake both.
    m_channel.close();
    for (Slot& slot : m_slots)
    {
        if (slot.session)
        {
            slot.session->kill();
        }
    }
    for (Slot& slot : m_slots)
    {
        if (slot.reader.joinable())
        {
            slot.reader.join();
        }
        slot.session.reset();
    }
}

void Gateway::release(std::uint32_t slot) noexcept
{
    m_slots[slot].state.store(SlotState::Finished, std::memory_order_release);
}

bool Gateway::claimSlot(std::uint32_t& slot)
{
    for (std::uint32_t i = 0; i < m_slots.size(); ++i)
    {
        const SlotState state = m_slots[i].state.load(std::memory_order_acquire);
        if (state == SlotState::Active)
        {
            continue;
        }
        if (state == SlotState::Finished)
        {
            // The old reader pushed its Disconnect and is exiting (or has exited).
            m_slots[i].reader.join();
            m_slots[i].session.reset();
        }
        slot = i;
        return true;
    }
    return false;
}

void Gateway::acceptLoop()
{
    while (!m_stopping.load(std::memory_order_acquire))
    {
        Socket client = m_listener.acceptWithin(kAcceptPollMillis);
        if (!client.valid())
        {
            continue; // Timeout: loop round and re-check the stop flag.
        }

        std::uint32_t slot = 0;
        if (!claimSlot(slot))
        {
            continue; // Full: `client` closes here, which refuses the connection.
        }
        m_slots[slot].session = std::make_unique<Session>(slot, std::move(client));
        m_slots[slot].state.store(SlotState::Active, std::memory_order_release);
        m_slots[slot].reader = std::thread(&Gateway::readLoop, this, slot);
    }
}

void Gateway::readLoop(std::uint32_t slot)
{
    const Session& session = *m_slots[slot].session;
    FrameReader frames;
    std::array<unsigned char, kReadBufferSize> buffer{};
    bool helloSeen = false;
    bool open = true;

    try
    {
        while (open)
        {
            const long received = session.socket().receive(buffer.data(), buffer.size());
            if (received <= 0)
            {
                break; // End of stream, error, or the engine killed the session.
            }
            frames.append(buffer.data(), static_cast<std::size_t>(received));

            Frame frame;
            while (open && frames.next(frame))
            {
                ClientMessage decoded;
                if (!decode(frame, decoded))
                {
                    open = false; // Malformed frame: drop the client.
                    break;
                }

                InboundMessage inbound;
                inbound.slot = slot;
                // The first frame must be Hello, and Hello may only come first.
                if (std::holds_alternative<Hello>(decoded) == helloSeen)
                {
                    open = false;
                    break;
                }
                std::visit(
                    [&inbound](const auto& message)
                    {
                        using Message = std::decay_t<decltype(message)>;
                        if constexpr (std::is_same_v<Message, Hello>)
                        {
                            inbound.kind = InboundKind::Connect;
                            inbound.agent = message.agent;
                            inbound.flags = message.flags;
                            inbound.version = message.version;
                        }
                        else if constexpr (std::is_same_v<Message, NewOrder>)
                        {
                            inbound.kind = InboundKind::Order;
                            inbound.command.type = CommandType::NewOrder;
                            inbound.command.clientOrderId = message.clientOrderId;
                            inbound.command.side = message.side;
                            inbound.command.orderType = message.orderType;
                            // The engine ignores the price of a market order; zero it so the
                            // command log does not depend on what the client happened to send.
                            inbound.command.price = message.orderType == OrderType::Limit ? message.price : 0;
                            inbound.command.quantity = message.quantity;
                        }
                        else if constexpr (std::is_same_v<Message, CancelOrder>)
                        {
                            inbound.kind = InboundKind::Order;
                            inbound.command.type = CommandType::Cancel;
                            inbound.command.clientOrderId = message.clientOrderId;
                            inbound.command.targetOrderId = message.targetOrderId;
                        }
                        else
                        {
                            inbound.kind = InboundKind::TickDone;
                            inbound.tick = message.tick;
                        }
                    },
                    decoded);
                helloSeen = true;
                if (!m_channel.push(inbound))
                {
                    return; // Channel closed: the exchange is shutting down.
                }
            }
        }
    }
    catch (const std::exception&)
    {
        // Oversized frame: fall through and report the disconnect.
    }

    InboundMessage goodbye;
    goodbye.kind = InboundKind::Disconnect;
    goodbye.slot = slot;
    m_channel.push(goodbye);
}

} // namespace agora::net
