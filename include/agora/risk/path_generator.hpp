/**
 * @file path_generator.hpp
 * @brief PathGenerator: simulates geometric Brownian motion, on the CPU or on a GPU.
 */
#pragma once

#include "agora/risk/option.hpp"

#include <cstdint>
#include <vector>

namespace agora::risk
{

/**
 * @brief Source of simulated prices for the Monte Carlo risk engine.
 *
 * Under geometric Brownian motion the price at the horizon has an exact
 * distribution,
 *
 *   S_T = S_0 * exp((drift - volatility^2 / 2) * T + volatility * sqrt(T) * Z),   Z ~ N(0, 1),
 *
 * so one normal draw gives one exact sample of S_T and no time stepping (and
 * no discretization error) is needed for payoffs that depend on S_T alone.
 *
 * Two implementations sit behind this interface: CpuPathGenerator (one
 * thread, the reference) and CudaPathGenerator (one GPU thread per group of
 * paths). OptionPricer and RiskMonitor work with either.
 */
class PathGenerator
{
  public:
    virtual ~PathGenerator() = default;

    /// "cpu" or "cuda", for logs and result files.
    virtual const char* name() const noexcept = 0;

    /**
     * @brief Fills @p terminal with independent samples of the price at the horizon.
     * @param seed Same seed, same samples (per implementation).
     */
    virtual void generate(const GbmParams& model, std::uint64_t seed, std::vector<double>& terminal) = 0;

    /**
     * @brief Simulates @p paths prices and sums the option payoff over them without storing them.
     *
     * This is what makes 10^8 paths practical: memory use does not grow with
     * the path count.
     *
     * @return Sum and sum of squares of the undiscounted payoff max(S_T - K, 0) or max(K - S_T, 0).
     */
    virtual PayoffMoments accumulate(const GbmParams& model, double strike, OptionType type,
                                     std::uint64_t paths, std::uint64_t seed) = 0;
};

/// Single-threaded reference implementation.
class CpuPathGenerator final : public PathGenerator
{
  public:
    const char* name() const noexcept override { return "cpu"; }
    void generate(const GbmParams& model, std::uint64_t seed, std::vector<double>& terminal) override;
    PayoffMoments accumulate(const GbmParams& model, double strike, OptionType type, std::uint64_t paths,
                             std::uint64_t seed) override;
};

} // namespace agora::risk
