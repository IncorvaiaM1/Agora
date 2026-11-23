/**
 * @file risk_monitor.cpp
 * @brief Implementation of RiskMonitor.
 */
#include "agora/risk/risk_monitor.hpp"

#include "agora/risk/black_scholes.hpp"

#include <cmath>

namespace agora::risk
{

RiskMonitor::RiskMonitor(PathGenerator& generator, RiskConfig config) : m_generator(generator), m_config(config)
{
}

void RiskMonitor::onTick(const net::Tick& tick)
{
    m_lastTick = tick.tick;

    // Mid price when both sides are quoted, otherwise the last trade. Before
    // the first quote or trade there is no market price to record.
    double price = 0.0;
    if (tick.bidPrice > 0 && tick.askPrice > 0)
    {
        price = 0.5 * static_cast<double>(tick.bidPrice + tick.askPrice);
    }
    else if (tick.lastTradePrice > 0)
    {
        price = static_cast<double>(tick.lastTradePrice);
    }
    if (price > 0.0)
    {
        m_prices.push_back(price);
    }
}

void RiskMonitor::onTrade(const net::Trade& trade)
{
    m_positions[trade.buyAgent] += trade.quantity;
    m_positions[trade.sellAgent] -= trade.quantity;
}

RiskReport RiskMonitor::report()
{
    RiskReport report;
    report.tick = m_lastTick;
    if (m_prices.empty())
    {
        return report;
    }
    report.spot = m_prices.back();
    report.volatility = realizedVolatility(m_prices, m_config.volatilityBar);

    // A different seed for every report and every use, all derived from the configured one.
    const std::uint64_t seed = m_config.seed + 1000 * m_reports++;

    // Options: at the money, so the strike is the current price.
    OptionSpec option;
    option.spot = report.spot;
    option.strike = report.spot;
    option.rate = 0.0;
    option.volatility = report.volatility;
    option.maturity = m_config.optionMaturity;

    OptionPricer pricer(m_generator);
    option.type = OptionType::Call;
    report.call = pricer.price(option, m_config.paths, seed);
    report.callExact = blackScholesPrice(option);
    option.type = OptionType::Put;
    report.put = pricer.price(option, m_config.paths, seed + 1);
    report.putExact = blackScholesPrice(option);

    // Tail risk: where could the price be at the risk horizon?
    const GbmParams model{report.spot, 0.0, report.volatility, m_config.riskHorizon};
    m_scratch.resize(m_config.paths);
    m_generator.generate(model, seed + 2, m_scratch);
    report.simulatedTails = simulatedTails(m_scratch, m_config.confidence);
    report.analyticTails = analyticTails(model, m_config.confidence);

    for (const auto& entry : m_positions)
    {
        AgentRisk risk;
        risk.agent = entry.first;
        risk.position = entry.second;
        risk.simulated = positionRisk(entry.second, report.spot, report.simulatedTails);
        risk.analytic = positionRisk(entry.second, report.spot, report.analyticTails);
        report.agents.push_back(risk);
    }
    return report;
}

} // namespace agora::risk
