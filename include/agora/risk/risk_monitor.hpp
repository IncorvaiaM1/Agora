/**
 * @file risk_monitor.hpp
 * @brief RiskMonitor: turns the live market data stream into risk numbers.
 */
#pragma once

#include "agora/net/protocol.hpp"
#include "agora/risk/option_pricer.hpp"
#include "agora/risk/path_generator.hpp"
#include "agora/risk/risk_metrics.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace agora::risk
{

/// RiskMonitor settings. Time is measured in simulation ticks throughout.
struct RiskConfig
{
    std::size_t volatilityBar = 10;      ///< Ticks per return when estimating volatility.
    double optionMaturity = 5000.0;      ///< Maturity of the at-the-money options that are priced.
    double riskHorizon = 1000.0;         ///< Look-ahead of value at risk and expected shortfall.
    double confidence = 0.99;            ///< Confidence level of the tail measures.
    std::uint64_t paths = 200'000;       ///< Simulated prices per estimate.
    std::uint64_t seed = 42;
};

/// One agent's position and what it could lose.
struct AgentRisk
{
    AgentId agent = 0;
    Quantity position = 0;
    TailRisk simulated; ///< From the Monte Carlo sample.
    TailRisk analytic;  ///< Closed form, as a check on the simulation.
};

/// Everything the monitor knows at one tick.
struct RiskReport
{
    std::uint64_t tick = 0;
    double spot = 0.0;
    double volatility = 0.0; ///< Realized, per square-root tick. 0 until enough prices have been seen.
    McEstimate call;         ///< At-the-money call, Monte Carlo.
    McEstimate put;          ///< At-the-money put, Monte Carlo.
    double callExact = 0.0;  ///< Black-Scholes value of the same call.
    double putExact = 0.0;
    PriceTails simulatedTails;
    PriceTails analyticTails;
    std::vector<AgentRisk> agents; ///< In agent id order.
};

/**
 * @brief Follows the exchange's market data and prices risk on demand.
 *
 * Feed it every Tick and Trade from the exchange. It keeps the price history
 * (for realized volatility) and every agent's net position (from the trade
 * tape). report() then
 *
 *  1. estimates volatility from the price history,
 *  2. prices an at-the-money call and put by Monte Carlo under geometric
 *     Brownian motion with that volatility, next to the Black-Scholes value,
 *  3. simulates the price at the risk horizon and computes each agent's value
 *     at risk and expected shortfall, next to the closed-form values.
 *
 * The interest rate and drift are zero: one tick is far too short for either
 * to matter. The heavy lifting is done by the PathGenerator, so the same
 * monitor runs on the CPU or on a GPU.
 */
class RiskMonitor
{
  public:
    /// @param generator Path source; must outlive the monitor.
    RiskMonitor(PathGenerator& generator, RiskConfig config);

    void onTick(const net::Tick& tick);
    void onTrade(const net::Trade& trade);

    /// Runs the simulations for the latest tick. Deterministic for a given message history and seed.
    RiskReport report();

    std::uint64_t ticksSeen() const noexcept { return m_lastTick; }
    const std::map<AgentId, Quantity>& positions() const noexcept { return m_positions; }

  private:
    PathGenerator& m_generator;
    RiskConfig m_config;
    std::vector<double> m_prices;           ///< One price per tick, once the market has a price.
    std::map<AgentId, Quantity> m_positions; ///< Ordered, so reports list agents in id order.
    std::vector<double> m_scratch;          ///< Reused sample buffer.
    std::uint64_t m_lastTick = 0;
    std::uint64_t m_reports = 0;
};

} // namespace agora::risk
