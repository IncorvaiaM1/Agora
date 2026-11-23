/**
 * @file option.hpp
 * @brief Plain data types shared by the risk engine: model parameters, option terms, estimates.
 *
 * Units: the risk engine does not care what one unit of time is, as long as
 * the rate, the volatility and the horizon agree. The convergence study uses
 * years (the textbook convention); the live risk monitor uses simulation
 * ticks, with volatility measured per square-root tick.
 */
#pragma once

#include <cstdint>

namespace agora::risk
{

/// Geometric Brownian motion: dS = drift * S dt + volatility * S dW.
struct GbmParams
{
    double spot = 100.0;      ///< Price now.
    double drift = 0.0;       ///< Expected return per unit time.
    double volatility = 0.2;  ///< Standard deviation of returns per square-root unit time.
    double horizon = 1.0;     ///< How far ahead to simulate.
};

enum class OptionType : std::uint8_t
{
    Call = 0,
    Put = 1
};

/// A European option: the right to buy (call) or sell (put) at the strike, at maturity only.
struct OptionSpec
{
    OptionType type = OptionType::Call;
    double spot = 100.0;
    double strike = 100.0;
    double rate = 0.0;        ///< Risk-free interest rate per unit time.
    double volatility = 0.2;
    double maturity = 1.0;
};

/// Raw sums from a batch of simulated payoffs; enough to get a mean and its uncertainty.
struct PayoffMoments
{
    double sum = 0.0;         ///< Sum of payoffs.
    double sumSquares = 0.0;  ///< Sum of squared payoffs.
    std::uint64_t paths = 0;  ///< Number of payoffs summed.
};

/// A Monte Carlo price with its statistical uncertainty.
struct McEstimate
{
    double price = 0.0;
    double standardError = 0.0;  ///< One standard deviation of the estimate; shrinks as 1/sqrt(paths).
    std::uint64_t paths = 0;
};

} // namespace agora::risk
