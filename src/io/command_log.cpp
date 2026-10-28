/**
 * @file command_log.cpp
 * @brief Implementation of CommandLogWriter and CommandLogReader.
 */
#include "agora/io/command_log.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace agora
{
namespace
{

constexpr char kMagic[8] = {'A', 'G', 'O', 'R', 'A', 'C', 'M', 'D'};
constexpr std::uint32_t kVersion = 1;

using Record = std::array<unsigned char, kCommandRecordSize>;

/// Writes the low @p bytes bytes of @p value into @p dst, least significant first.
void putLittleEndian(unsigned char* dst, std::uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        dst[i] = static_cast<unsigned char>(value >> (8 * i));
    }
}

/// Reads @p bytes little-endian bytes from @p src.
std::uint64_t getLittleEndian(const unsigned char* src, std::size_t bytes)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i)
    {
        value |= static_cast<std::uint64_t>(src[i]) << (8 * i);
    }
    return value;
}

} // namespace

CommandLogWriter::CommandLogWriter(std::ostream& out) : m_out(out)
{
    unsigned char version[4];
    putLittleEndian(version, kVersion, 4);
    m_out.write(kMagic, sizeof(kMagic));
    m_out.write(reinterpret_cast<const char*>(version), sizeof(version));
    if (!m_out)
    {
        throw std::runtime_error("CommandLogWriter: failed to write header");
    }
}

void CommandLogWriter::write(const Command& command)
{
    Record record{};
    record[0] = static_cast<unsigned char>(command.type);
    record[1] = static_cast<unsigned char>(command.side);
    record[2] = static_cast<unsigned char>(command.orderType);
    record[3] = 0;
    putLittleEndian(&record[4], command.agent, 4);
    putLittleEndian(&record[8], command.clientOrderId, 8);
    putLittleEndian(&record[16], static_cast<std::uint64_t>(command.price), 8);
    putLittleEndian(&record[24], static_cast<std::uint64_t>(command.quantity), 8);
    putLittleEndian(&record[32], command.targetOrderId, 8);

    m_out.write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
    if (!m_out)
    {
        throw std::runtime_error("CommandLogWriter: write failed");
    }
    ++m_count;
}

CommandLogReader::CommandLogReader(std::istream& in) : m_in(in)
{
    char magic[8];
    unsigned char version[4];
    m_in.read(magic, sizeof(magic));
    m_in.read(reinterpret_cast<char*>(version), sizeof(version));
    if (!m_in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
    {
        throw std::runtime_error("CommandLogReader: not an Agora command log");
    }
    if (getLittleEndian(version, 4) != kVersion)
    {
        throw std::runtime_error("CommandLogReader: unsupported log version");
    }
}

bool CommandLogReader::read(Command& command)
{
    Record record{};
    m_in.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()));
    const std::streamsize got = m_in.gcount();
    if (got == 0 && m_in.eof())
    {
        return false;
    }
    if (got != static_cast<std::streamsize>(record.size()))
    {
        throw std::runtime_error("CommandLogReader: truncated record");
    }

    command.type = static_cast<CommandType>(record[0]);
    command.side = static_cast<Side>(record[1]);
    command.orderType = static_cast<OrderType>(record[2]);
    command.agent = static_cast<AgentId>(getLittleEndian(&record[4], 4));
    command.clientOrderId = getLittleEndian(&record[8], 8);
    command.price = static_cast<Price>(getLittleEndian(&record[16], 8));
    command.quantity = static_cast<Quantity>(getLittleEndian(&record[24], 8));
    command.targetOrderId = getLittleEndian(&record[32], 8);
    return true;
}

} // namespace agora
