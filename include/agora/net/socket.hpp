/**
 * @file socket.hpp
 * @brief Socket: a small RAII wrapper over a POSIX TCP socket.
 *
 * Only the plain BSD socket API is used (no third-party networking library),
 * so the exchange and agents build on PACE-ICE with nothing but a compiler.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace agora::net
{

/**
 * @brief Owns one socket file descriptor and closes it on destruction.
 *
 * Move-only. The same Socket may be read by one thread and written by
 * another at the same time (the kernel allows that); close() must not race
 * with either.
 */
class Socket
{
  public:
    Socket() noexcept = default;
    explicit Socket(int fd) noexcept : m_fd(fd) {}
    ~Socket();

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    /**
     * @brief Creates a listening TCP socket.
     * @param host Local address to bind, e.g. "127.0.0.1" or "0.0.0.0".
     * @param port Port to bind; 0 lets the OS pick one (see localPort()).
     * @throws std::runtime_error on failure.
     */
    static Socket listenOn(const std::string& host, std::uint16_t port, int backlog = 128);

    /**
     * @brief Connects to a TCP server and disables Nagle's algorithm.
     * @throws std::runtime_error if the host cannot be resolved or reached.
     */
    static Socket connectTo(const std::string& host, std::uint16_t port);

    /**
     * @brief Waits up to @p timeoutMillis for a connection on a listening socket.
     * @return The accepted socket, or an invalid Socket on timeout or error.
     */
    Socket acceptWithin(int timeoutMillis) const;

    /// Port this socket is bound to. @throws std::runtime_error on failure.
    std::uint16_t localPort() const;

    /// Disables Nagle's algorithm so small frames are sent immediately.
    void setNoDelay() const noexcept;

    /// Makes a blocked send fail after @p millis, so one stuck client cannot stall the sender.
    void setSendTimeout(int millis) const noexcept;

    /// Sends the whole buffer. @return false on any error (including timeout).
    bool sendAll(const unsigned char* data, std::size_t size) const noexcept;

    /// Receives up to @p capacity bytes. @return bytes read, 0 at end of stream, -1 on error.
    long receive(unsigned char* buffer, std::size_t capacity) const noexcept;

    /// Shuts down both directions, waking any thread blocked in receive(). Keeps the descriptor.
    void shutdownBoth() const noexcept;

    void close() noexcept;
    bool valid() const noexcept { return m_fd >= 0; }
    int fd() const noexcept { return m_fd; }

  private:
    int m_fd = -1;
};

} // namespace agora::net
