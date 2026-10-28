/**
 * @file trade_log.hpp
 * @brief TradeLogWriter: writes every trade as one CSV line.
 *
 * The trade log is the artifact compared byte for byte in the determinism
 * check (result 6), and the input to the market statistics (result 3).
 * Only integers are written, so the output is identical on every platform.
 */
#pragma once

#include "agora/core/events.hpp"

#include <ostream>

namespace agora
{

/// EventListener that appends trades to a CSV stream.
class TradeLogWriter final : public EventListener
{
  public:
    /// Writes the CSV header immediately.
    explicit TradeLogWriter(std::ostream& out);

    void onTrade(const TradeEvent& event) override;

  private:
    std::ostream& m_out;
};

} // namespace agora
