/**
 * @file option_pricer.cpp
 * @brief Implementation of OptionPricer.
 */
#include "agora/risk/option_pricer.hpp"

#include <algorithm>
#include <cmath>

namespace agora::risk
{

McEstimate OptionPricer::price(const OptionSpec& option, std::uint64_t paths, std::uint64_t seed)
{
    // Risk-neutral pricing: the underlying drifts at the risk-free rate.
    const GbmParams model{option.spot, option.rate, option.volatility, option.maturity};
    const PayoffMoments moments = m_generator.accumulate(model, option.strike, option.type, paths, seed);
    return estimate(moments, std::exp(-option.rate * option.maturity));
}

McEstimate OptionPricer::estimate(const PayoffMoments& moments, double discount) noexcept
{
    McEstimate result;
    result.paths = moments.paths;
    if (moments.paths == 0)
    {
        return result;
    }
    const auto count = static_cast<double>(moments.paths);
    const double mean = moments.sum / count;
    result.price = discount * mean;
    if (moments.paths > 1)
    {
        // Unbiased sample variance; clamped because rounding can push it a hair below zero.
        const double variance = std::max((moments.sumSquares - count * mean * mean) / (count - 1.0), 0.0);
        result.standardError = discount * std::sqrt(variance / count);
    }
    return result;
}

} // namespace agora::risk
