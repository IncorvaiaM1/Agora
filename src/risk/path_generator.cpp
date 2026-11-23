/**
 * @file path_generator.cpp
 * @brief CpuPathGenerator: the single-threaded reference path generator.
 */
#include "agora/risk/path_generator.hpp"

#include "agora/core/rng.hpp"

#include <algorithm>
#include <cmath>

namespace agora::risk
{
namespace
{

constexpr double kTwoPi = 6.283185307179586476925286766559;

/// Standard normal draws from agora::Rng by the Box-Muller transform.
class NormalSource
{
  public:
    explicit NormalSource(std::uint64_t seed) : m_rng(seed) {}

    double next() noexcept
    {
        // Each transform yields two independent normals; hand out the second on the next call.
        if (m_hasSpare)
        {
            m_hasSpare = false;
            return m_spare;
        }
        // 1 - uniform() lies in (0, 1], so the logarithm is always finite.
        const double radius = std::sqrt(-2.0 * std::log(1.0 - m_rng.uniform()));
        const double angle = kTwoPi * m_rng.uniform();
        m_spare = radius * std::sin(angle);
        m_hasSpare = true;
        return radius * std::cos(angle);
    }

  private:
    Rng m_rng;
    double m_spare = 0.0;
    bool m_hasSpare = false;
};

/// The two constants of the exact solution: S_T = spot * exp(shift + scale * Z).
struct LogNormal
{
    double shift;
    double scale;
};

LogNormal logNormalOf(const GbmParams& model) noexcept
{
    return LogNormal{(model.drift - 0.5 * model.volatility * model.volatility) * model.horizon,
                     model.volatility * std::sqrt(model.horizon)};
}

} // namespace

void CpuPathGenerator::generate(const GbmParams& model, std::uint64_t seed, std::vector<double>& terminal)
{
    const LogNormal law = logNormalOf(model);
    NormalSource normals(seed);
    for (double& price : terminal)
    {
        price = model.spot * std::exp(law.shift + law.scale * normals.next());
    }
}

PayoffMoments CpuPathGenerator::accumulate(const GbmParams& model, double strike, OptionType type,
                                           std::uint64_t paths, std::uint64_t seed)
{
    const LogNormal law = logNormalOf(model);
    const bool isCall = type == OptionType::Call;
    NormalSource normals(seed);

    PayoffMoments moments;
    moments.paths = paths;
    for (std::uint64_t i = 0; i < paths; ++i)
    {
        const double price = model.spot * std::exp(law.shift + law.scale * normals.next());
        const double payoff = std::max(isCall ? price - strike : strike - price, 0.0);
        moments.sum += payoff;
        moments.sumSquares += payoff * payoff;
    }
    return moments;
}

} // namespace agora::risk
