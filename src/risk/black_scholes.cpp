/**
 * @file black_scholes.cpp
 * @brief Normal distribution helpers and the Black-Scholes formula.
 */
#include "agora/risk/black_scholes.hpp"

#include <algorithm>
#include <cmath>

namespace agora::risk
{

double normalCdf(double x) noexcept
{
    // erfc keeps full precision in the lower tail, where 1 + erf(x) would cancel.
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

double normalQuantile(double p) noexcept
{
    // Bisection on the CDF. Slower than a rational approximation, but exact to
    // double precision and only called a handful of times per report.
    double low = -40.0;
    double high = 40.0;
    for (int i = 0; i < 200; ++i)
    {
        const double middle = 0.5 * (low + high);
        if (normalCdf(middle) < p)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return 0.5 * (low + high);
}

double blackScholesPrice(const OptionSpec& option) noexcept
{
    const double discount = std::exp(-option.rate * option.maturity);
    const double spread = option.volatility * std::sqrt(option.maturity);
    const bool isCall = option.type == OptionType::Call;

    if (spread <= 0.0)
    {
        // No randomness left: the option is worth its discounted payoff at the forward price.
        const double forward = option.spot / discount;
        const double payoff = isCall ? forward - option.strike : option.strike - forward;
        return discount * std::max(payoff, 0.0);
    }

    const double d1 =
        (std::log(option.spot / option.strike) + (option.rate + 0.5 * option.volatility * option.volatility) * option.maturity) /
        spread;
    const double d2 = d1 - spread;

    if (isCall)
    {
        return option.spot * normalCdf(d1) - option.strike * discount * normalCdf(d2);
    }
    return option.strike * discount * normalCdf(-d2) - option.spot * normalCdf(-d1);
}

} // namespace agora::risk
