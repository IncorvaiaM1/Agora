/**
 * @file risk_metrics.cpp
 * @brief Implementation of the volatility and tail-risk measures.
 */
#include "agora/risk/risk_metrics.hpp"

#include "agora/risk/black_scholes.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace agora::risk
{

double realizedVolatility(const std::vector<double>& prices, std::size_t bar)
{
    if (bar == 0 || prices.size() < 2 * bar + 1)
    {
        return 0.0;
    }

    double sum = 0.0;
    double sumSquares = 0.0;
    std::size_t count = 0;
    for (std::size_t i = bar; i < prices.size(); i += bar)
    {
        const double logReturn = std::log(prices[i] / prices[i - bar]);
        sum += logReturn;
        sumSquares += logReturn * logReturn;
        ++count;
    }
    const auto n = static_cast<double>(count);
    const double mean = sum / n;
    const double variance = std::max((sumSquares - n * mean * mean) / (n - 1.0), 0.0);
    return std::sqrt(variance / static_cast<double>(bar));
}

PriceTails simulatedTails(std::vector<double>& samples, double confidence)
{
    PriceTails tails;
    const std::size_t count = samples.size();
    if (count == 0)
    {
        return tails;
    }
    // Number of samples in each tail: the worst (1 - confidence) share, at least one.
    // Rounded, not truncated: 1 - 0.9 is slightly below 0.1 in binary floating point.
    const auto tailSize = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::llround((1.0 - confidence) * static_cast<double>(count))), 1, count);
    const auto tail = static_cast<std::ptrdiff_t>(tailSize);

    // Partial sorts: only the tails need to be separated, not the whole sample ordered.
    std::nth_element(samples.begin(), samples.begin() + (tail - 1), samples.end());
    tails.lowQuantile = samples[tailSize - 1];
    tails.lowMean = std::accumulate(samples.begin(), samples.begin() + tail, 0.0) / static_cast<double>(tailSize);

    std::nth_element(samples.begin(), samples.end() - tail, samples.end());
    tails.highQuantile = samples[count - tailSize];
    tails.highMean = std::accumulate(samples.end() - tail, samples.end(), 0.0) / static_cast<double>(tailSize);
    return tails;
}

PriceTails analyticTails(const GbmParams& model, double confidence)
{
    // S_T = spot * exp(shift + scale * Z). Quantiles follow from those of Z; the
    // tail means from the lognormal partial expectation
    //   E[S_T ; Z < z] = spot * exp(drift * T) * Phi(z - scale).
    const double alpha = 1.0 - confidence;
    const double shift = (model.drift - 0.5 * model.volatility * model.volatility) * model.horizon;
    const double scale = model.volatility * std::sqrt(model.horizon);
    const double forward = model.spot * std::exp(model.drift * model.horizon);
    const double zLow = normalQuantile(alpha);
    const double zHigh = -zLow;

    PriceTails tails;
    tails.lowQuantile = model.spot * std::exp(shift + scale * zLow);
    tails.lowMean = forward * normalCdf(zLow - scale) / alpha;
    tails.highQuantile = model.spot * std::exp(shift + scale * zHigh);
    tails.highMean = forward * normalCdf(scale - zHigh) / alpha;
    return tails;
}

TailRisk positionRisk(Quantity position, double spot, const PriceTails& tails) noexcept
{
    const auto shares = static_cast<double>(position);
    TailRisk risk;
    if (position > 0)
    {
        risk.valueAtRisk = shares * (spot - tails.lowQuantile);
        risk.expectedShortfall = shares * (spot - tails.lowMean);
    }
    else if (position < 0)
    {
        risk.valueAtRisk = -shares * (tails.highQuantile - spot);
        risk.expectedShortfall = -shares * (tails.highMean - spot);
    }
    return risk;
}

} // namespace agora::risk
