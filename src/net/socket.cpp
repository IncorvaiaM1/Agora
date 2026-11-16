/**
 * @file socket.cpp
 * @brief POSIX implementation of Socket.
 */
#include "agora/net/socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace agora::net
{
namespace
{

// Writing to a socket whose peer has gone away raises SIGPIPE by default,
// which would kill the exchange. Linux suppresses it per call; macOS per socket.
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void suppressSigpipe(int fd) noexcept
{
#ifdef SO_NOSIGPIPE
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
}

std::runtime_error systemError(const std::string& what)
{
    return std::runtime_error(what + ": " + std::strerror(errno));
}

} // namespace

Socket::~Socket()
{
    close();
}

Socket::Socket(Socket&& other) noexcept : m_fd(other.m_fd)
{
    other.m_fd = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

Socket Socket::listenOn(const std::string& host, std::uint16_t port, int backlog)
{
    Socket socket(::socket(AF_INET, SOCK_STREAM, 0));
    if (!socket.valid())
    {
        throw systemError("socket");
    }

    // Allow a quick restart on the same port while old connections linger in TIME_WAIT.
    const int on = 1;
    ::setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
    {
        throw std::runtime_error("listenOn: bad bind address " + host);
    }
    if (::bind(socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        throw systemError("bind " + host + ":" + std::to_string(port));
    }
    if (::listen(socket.fd(), backlog) != 0)
    {
        throw systemError("listen");
    }
    return socket;
}

Socket Socket::connectTo(const std::string& host, std::uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const int status = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
    if (status != 0)
    {
        throw std::runtime_error("connectTo: cannot resolve " + host + ": " + ::gai_strerror(status));
    }

    Socket socket;
    for (const addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next)
    {
        Socket attempt(::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol));
        if (!attempt.valid())
        {
            continue;
        }
        if (::connect(attempt.fd(), candidate->ai_addr, candidate->ai_addrlen) == 0)
        {
            socket = std::move(attempt);
            break;
        }
    }
    ::freeaddrinfo(results);

    if (!socket.valid())
    {
        throw std::runtime_error("connectTo: cannot connect to " + host + ":" + std::to_string(port));
    }
    suppressSigpipe(socket.fd());
    socket.setNoDelay();
    return socket;
}

Socket Socket::acceptWithin(int timeoutMillis) const
{
    pollfd waiter{};
    waiter.fd = m_fd;
    waiter.events = POLLIN;
    if (::poll(&waiter, 1, timeoutMillis) <= 0)
    {
        return Socket();
    }
    Socket client(::accept(m_fd, nullptr, nullptr));
    if (client.valid())
    {
        suppressSigpipe(client.fd());
    }
    return client;
}

std::uint16_t Socket::localPort() const
{
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(m_fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        throw systemError("getsockname");
    }
    return ntohs(address.sin_port);
}

void Socket::setNoDelay() const noexcept
{
    const int on = 1;
    ::setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

void Socket::setSendTimeout(int millis) const noexcept
{
    timeval timeout{};
    timeout.tv_sec = millis / 1000;
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((millis % 1000) * 1000);
    ::setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

bool Socket::sendAll(const unsigned char* data, std::size_t size) const noexcept
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t result = ::send(m_fd, data + sent, size - sent, kSendFlags);
        if (result > 0)
        {
            sent += static_cast<std::size_t>(result);
        }
        else if (result < 0 && errno == EINTR)
        {
            continue; // Interrupted by a signal before anything was sent; try again.
        }
        else
        {
            return false;
        }
    }
    return true;
}

long Socket::receive(unsigned char* buffer, std::size_t capacity) const noexcept
{
    for (;;)
    {
        const ssize_t result = ::recv(m_fd, buffer, capacity, 0);
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return static_cast<long>(result);
    }
}

void Socket::shutdownBoth() const noexcept
{
    if (m_fd >= 0)
    {
        ::shutdown(m_fd, SHUT_RDWR);
    }
}

void Socket::close() noexcept
{
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace agora::net
