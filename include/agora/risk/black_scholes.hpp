/**
 * @file black_scholes.hpp
 * @brief Closed-form results used to validate the Monte Carlo engine.
 */
#pragma once

#include "agora/risk/option.hpp"

namespace agora::risk
{

/// Standard normal cumulative distribution function.
double normalCdf(double x) noexcept;

/// Inverse of normalCdf: the z with normalCdf(z) = p. @pre 0 < p < 1.
double normalQuantile(double p) noexcept;

/**
 * @brief Black-Scholes price of a European option.
 *
 * The exact answer the Monte Carlo price must converge to (result 4).
 * A maturity or volatility of zero returns the discounted intrinsic value.
 */
double blackScholesPrice(const OptionSpec& option) noexcept;

} // namespace agora::risk
