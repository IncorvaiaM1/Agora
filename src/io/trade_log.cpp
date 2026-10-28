/**
 * @file trade_log.cpp
 * @brief Implementation of TradeLogWriter.
 */
#include "agora/io/trade_log.hpp"

namespace agora
{

TradeLogWriter::TradeLogWriter(std::ostream& out) : m_out(out)
{
    m_out << "sequence,trade_id,buy_order,sell_order,buy_agent,sell_agent,price,quantity,aggressor\n";
}

void TradeLogWriter::onTrade(const TradeEvent& event)
{
    m_out << event.sequence << ',' << event.tradeId << ',' << event.buyOrderId << ',' << event.sellOrderId
          << ',' << event.buyAgent << ',' << event.sellAgent << ',' << event.price << ',' << event.quantity
          << ',' << (event.aggressor == Side::Buy ? 'B' : 'S') << '\n';
}

} // namespace agora
