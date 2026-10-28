/**
 * @file command_log.hpp
 * @brief Binary log of sequenced commands, for exact replay.
 *
 * File format (all integers little-endian, independent of host byte order):
 *
 *   header : "AGORACMD" (8 bytes) | version u32 (= 1)
 *   record : type u8 | side u8 | orderType u8 | reserved u8 | agent u32 |
 *            clientOrderId u64 | price i64 | quantity i64 | targetOrderId u64
 *            (40 bytes)
 *
 * Records appear in the order the engine sequenced them, so feeding them back
 * into a fresh MatchingEngine reproduces the session exactly.
 */
#pragma once

#include "agora/core/command.hpp"

#include <cstddef>
#include <istream>
#include <ostream>

namespace agora
{

/// Size of one encoded command record in bytes.
constexpr std::size_t kCommandRecordSize = 40;

/// Appends commands to a binary stream. Writes the header on construction.
class CommandLogWriter
{
  public:
    /// @throws std::runtime_error if the header cannot be written.
    explicit CommandLogWriter(std::ostream& out);

    /// Appends one command. @throws std::runtime_error on stream failure.
    void write(const Command& command);

    std::size_t count() const noexcept { return m_count; }

  private:
    std::ostream& m_out;
    std::size_t m_count = 0;
};

/// Reads commands back from a binary stream. Validates the header on construction.
class CommandLogReader
{
  public:
    /// @throws std::runtime_error if the header is missing or has the wrong version.
    explicit CommandLogReader(std::istream& in);

    /**
     * @brief Reads the next command.
     * @return false at a clean end of stream.
     * @throws std::runtime_error on a truncated record.
     */
    bool read(Command& command);

  private:
    std::istream& m_in;
};

} // namespace agora
