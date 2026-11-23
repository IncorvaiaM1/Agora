/**
 * @file risk_metrics.hpp
 * @brief Realized volatility, value at risk and expected shortfall.
 */
#pragma once

#include "agora/core/types.hpp"
#include "agora/risk/option.hpp"

#include <cstddef>
#include <vector>

namespace agora::risk
{

/**
 * @brief Volatility per square-root tick, estimated from a price series.
 *
 * Takes log returns between prices @p bar ticks apart, and scales their
 * standard deviation back to one tick by dividing by sqrt(bar).
 *
 * @return 0 if the series is too short to give two returns.
 */
double realizedVolatility(const std::vector<double>& prices, std::size_t bar);

/// The two tails of a distribution of future prices, at one confidence level.
struct PriceTails
{
    double lowQuantile = 0.0;  ///< Price exceeded with probability `confidence`.
    double lowMean = 0.0;      ///< Average price in the tail below lowQuantile.
    double highQuantile = 0.0; ///< Price not exceeded with probability `confidence`.
    double highMean = 0.0;     ///< Average price in the tail above highQuantile.
};

/**
 * @brief Measures both tails from simulated prices.
 * @param samples Simulated future prices; reordered by this call.
 * @param confidence For example 0.99.
 */
PriceTails simulatedTails(std::vector<double>& samples, double confidence);

/// The same tails in closed form for geometric Brownian motion, to validate the simulation.
PriceTails analyticTails(const GbmParams& model, double confidence);

/// Loss measures for one position, in price ticks. Both are positive for a loss.
struct TailRisk
{
    double valueAtRisk = 0.0;       ///< Loss exceeded only with probability 1 - confidence.
    double expectedShortfall = 0.0; ///< Average loss in those worst cases; never below valueAtRisk.
};

/**
 * @brief Value at risk and expected shortfall of holding @p position shares.
 *
 * A long position loses when the price falls, so it reads the low tail; a
 * short position loses when the price rises and reads the high tail.
 */
TailRisk positionRisk(Quantity position, double spot, const PriceTails& tails) noexcept;

} // namespace agora::risk
