/**
 * @file option_pricer.hpp
 * @brief OptionPricer: Monte Carlo pricing of European options.
 */
#pragma once

#include "agora/risk/option.hpp"
#include "agora/risk/path_generator.hpp"

namespace agora::risk
{

/**
 * @brief Prices European options by Monte Carlo on any PathGenerator.
 *
 * The price of an option is the discounted average payoff when the
 * underlying grows at the risk-free rate. The pricer asks the generator for
 * the payoff sums, then turns them into a price and a standard error:
 *
 *   price = exp(-r T) * mean(payoff)
 *   error = exp(-r T) * stddev(payoff) / sqrt(paths)
 *
 * The standard error is what result 4 checks: it must fall as 1/sqrt(paths)
 * and the true (Black-Scholes) price must lie within a few of them.
 */
class OptionPricer
{
  public:
    /// @param generator Path source; must outlive the pricer.
    explicit OptionPricer(PathGenerator& generator) : m_generator(generator) {}

    /// @param paths Number of simulated prices (>= 2). @param seed Same seed, same estimate.
    McEstimate price(const OptionSpec& option, std::uint64_t paths, std::uint64_t seed);

    /// Turns raw payoff sums into a discounted price and its standard error.
    static McEstimate estimate(const PayoffMoments& moments, double discount) noexcept;

  private:
    PathGenerator& m_generator;
};

} // namespace agora::risk
