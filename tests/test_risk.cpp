// Risk engine: closed forms, the CPU path generator, the Monte Carlo pricer
// and the tail-risk measures. The CUDA generator shares these checks when the
// build has CUDA (see the last test case).
#include "agora/risk/black_scholes.hpp"
#include "agora/risk/option_pricer.hpp"
#include "agora/risk/path_generator.hpp"
#include "agora/risk/risk_metrics.hpp"
#include "agora/risk/risk_monitor.hpp"

#ifdef AGORA_HAS_CUDA
#include "agora/risk/cuda_path_generator.hpp"
#endif

#include "doctest/doctest.h"

#include <cmath>
#include <string>
#include <vector>

using namespace agora::risk;

namespace
{

OptionSpec textbookOption(OptionType type)
{
    OptionSpec option;
    option.type = type;
    option.spot = 100.0;
    option.strike = 105.0;
    option.rate = 0.05;
    option.volatility = 0.2;
    option.maturity = 1.0;
    return option;
}

/// The checks every PathGenerator must pass, whatever hardware it runs on.
void checkGenerator(PathGenerator& generator)
{
    CAPTURE(generator.name());

    // Terminal prices have the lognormal mean and variance.
    const GbmParams model{100.0, 0.05, 0.2, 1.0};
    std::vector<double> prices(400'000);
    generator.generate(model, 7, prices);
    double sum = 0.0;
    double sumSquares = 0.0;
    for (const double price : prices)
    {
        REQUIRE(price > 0.0);
        sum += price;
        sumSquares += price * price;
    }
    const auto count = static_cast<double>(prices.size());
    const double mean = sum / count;
    const double variance = sumSquares / count - mean * mean;
    const double expectedMean = 100.0 * std::exp(0.05);
    const double expectedVariance = expectedMean * expectedMean * (std::exp(0.2 * 0.2) - 1.0);
    CHECK(mean == doctest::Approx(expectedMean).epsilon(0.002));
    CHECK(variance == doctest::Approx(expectedVariance).epsilon(0.02));

    // Same seed, same samples; a different seed gives different ones.
    std::vector<double> again(1000);
    std::vector<double> first(1000);
    std::vector<double> other(1000);
    generator.generate(model, 3, first);
    generator.generate(model, 3, again);
    generator.generate(model, 4, other);
    CHECK(first == again);
    CHECK(first != other);

    // Monte Carlo prices agree with Black-Scholes within their own error bars.
    OptionPricer pricer(generator);
    for (const OptionType type : {OptionType::Call, OptionType::Put})
    {
        const OptionSpec option = textbookOption(type);
        const double exact = blackScholesPrice(option);
        const McEstimate estimate = pricer.price(option, 1'000'000, 11);
        CHECK(estimate.paths == 1'000'000);
        CHECK(estimate.standardError > 0.0);
        CHECK(std::fabs(estimate.price - exact) < 4.0 * estimate.standardError);
        CHECK(estimate.price == doctest::Approx(exact).epsilon(0.01));
    }

    // 100 times the paths gives about 10 times less error: the 1/sqrt(N) law.
    const OptionSpec call = textbookOption(OptionType::Call);
    const McEstimate coarse = pricer.price(call, 10'000, 5);
    const McEstimate fine = pricer.price(call, 1'000'000, 5);
    CHECK(coarse.standardError / fine.standardError == doctest::Approx(10.0).epsilon(0.1));

    // A path count that is not a multiple of any batch size is still honoured exactly.
    CHECK(generator.accumulate(model, 105.0, OptionType::Call, 12'345, 1).paths == 12'345);
    CHECK(generator.accumulate(model, 1e9, OptionType::Call, 12'345, 1).sum == 0.0); // never in the money
    const PayoffMoments deep = generator.accumulate(model, 0.0, OptionType::Call, 200'000, 1);
    CHECK(deep.sum / 200'000.0 == doctest::Approx(expectedMean).epsilon(0.005)); // strike 0: payoff is the price
}

} // namespace

TEST_CASE("risk: normal distribution helpers")
{
    CHECK(normalCdf(0.0) == doctest::Approx(0.5));
    CHECK(normalCdf(1.0) == doctest::Approx(0.8413447461));
    CHECK(normalCdf(-1.96) == doctest::Approx(0.0249978952));
    CHECK(normalCdf(-8.0) == doctest::Approx(6.220960574e-16).epsilon(1e-6)); // deep tail keeps precision

    CHECK(normalQuantile(0.5) == doctest::Approx(0.0).scale(1.0));
    CHECK(normalQuantile(0.975) == doctest::Approx(1.959963985));
    CHECK(normalQuantile(0.01) == doctest::Approx(-2.326347874));
    for (const double p : {0.001, 0.2, 0.7, 0.999})
    {
        CHECK(normalCdf(normalQuantile(p)) == doctest::Approx(p));
    }
}

TEST_CASE("risk: Black-Scholes prices")
{
    // Reference values for S=100, K=105, r=5%, sigma=20%, T=1.
    const OptionSpec call = textbookOption(OptionType::Call);
    const OptionSpec put = textbookOption(OptionType::Put);
    CHECK(blackScholesPrice(call) == doctest::Approx(8.021352235));
    CHECK(blackScholesPrice(put) == doctest::Approx(7.900441651));

    // Put-call parity: C - P = S - K exp(-rT), for any inputs.
    for (const double strike : {60.0, 100.0, 105.0, 180.0})
    {
        OptionSpec c = call;
        OptionSpec p = put;
        c.strike = strike;
        p.strike = strike;
        CHECK(blackScholesPrice(c) - blackScholesPrice(p) ==
              doctest::Approx(100.0 - strike * std::exp(-0.05)).epsilon(1e-10));
    }

    // No volatility: the option is worth its discounted payoff at the forward price.
    OptionSpec flat = call;
    flat.volatility = 0.0;
    flat.strike = 90.0;
    CHECK(blackScholesPrice(flat) == doctest::Approx(100.0 - 90.0 * std::exp(-0.05)));
    flat.strike = 200.0;
    CHECK(blackScholesPrice(flat) == 0.0);

    // More volatility, more value.
    OptionSpec wild = call;
    wild.volatility = 0.4;
    CHECK(blackScholesPrice(wild) > blackScholesPrice(call));
}

TEST_CASE("risk: estimate turns payoff sums into a price and a standard error")
{
    // Payoffs 0, 2, 4, 6: mean 3, sample variance 20/3.
    PayoffMoments moments;
    moments.paths = 4;
    moments.sum = 12.0;
    moments.sumSquares = 56.0;
    const McEstimate estimate = OptionPricer::estimate(moments, 0.5);
    CHECK(estimate.price == doctest::Approx(1.5));
    CHECK(estimate.standardError == doctest::Approx(0.5 * std::sqrt(20.0 / 3.0 / 4.0)));
    CHECK(OptionPricer::estimate(PayoffMoments{}, 1.0).price == 0.0);
}

TEST_CASE("risk: CPU path generator")
{
    CpuPathGenerator generator;
    checkGenerator(generator);
}

TEST_CASE("risk: realized volatility recovers the volatility of simulated prices")
{
    // Build a price path with a known per-tick volatility out of the generator itself.
    const double perTick = 0.0004;
    CpuPathGenerator generator;
    std::vector<double> steps(50'000);
    generator.generate(GbmParams{1.0, 0.0, perTick, 1.0}, 9, steps); // each sample is one tick's growth factor
    std::vector<double> prices{10'000.0};
    for (const double step : steps)
    {
        prices.push_back(prices.back() * step);
    }
    CHECK(realizedVolatility(prices, 1) == doctest::Approx(perTick).epsilon(0.02));
    CHECK(realizedVolatility(prices, 10) == doctest::Approx(perTick).epsilon(0.05)); // independent steps: same per tick

    CHECK(realizedVolatility({100.0, 101.0}, 10) == 0.0); // too short
    CHECK(realizedVolatility(std::vector<double>(100, 50.0), 5) == 0.0); // constant price
}

TEST_CASE("risk: simulated tails match the closed form")
{
    const GbmParams model{10'000.0, 0.0, 0.0005, 1000.0};
    CpuPathGenerator generator;
    std::vector<double> samples(500'000);
    generator.generate(model, 21, samples);

    const PriceTails simulated = simulatedTails(samples, 0.99);
    const PriceTails exact = analyticTails(model, 0.99);
    CHECK(exact.lowMean < exact.lowQuantile);
    CHECK(exact.lowQuantile < model.spot);
    CHECK(exact.highQuantile > model.spot);
    CHECK(exact.highMean > exact.highQuantile);
    // Compare distances from the spot, which is what risk is measured in.
    CHECK(model.spot - simulated.lowQuantile == doctest::Approx(model.spot - exact.lowQuantile).epsilon(0.02));
    CHECK(model.spot - simulated.lowMean == doctest::Approx(model.spot - exact.lowMean).epsilon(0.02));
    CHECK(simulated.highQuantile - model.spot == doctest::Approx(exact.highQuantile - model.spot).epsilon(0.02));
    CHECK(simulated.highMean - model.spot == doctest::Approx(exact.highMean - model.spot).epsilon(0.02));

    // Small hand-checkable sample: 1..100 at 90% confidence leaves 10 in each tail.
    std::vector<double> ladder;
    for (int i = 100; i >= 1; --i)
    {
        ladder.push_back(static_cast<double>(i));
    }
    const PriceTails tails = simulatedTails(ladder, 0.90);
    CHECK(tails.lowQuantile == 10.0);
    CHECK(tails.lowMean == doctest::Approx(5.5));
    CHECK(tails.highQuantile == 91.0);
    CHECK(tails.highMean == doctest::Approx(95.5));
}

TEST_CASE("risk: position risk reads the right tail")
{
    PriceTails tails;
    tails.lowQuantile = 90.0;
    tails.lowMean = 85.0;
    tails.highQuantile = 112.0;
    tails.highMean = 120.0;

    const TailRisk longRisk = positionRisk(10, 100.0, tails);
    CHECK(longRisk.valueAtRisk == doctest::Approx(100.0));
    CHECK(longRisk.expectedShortfall == doctest::Approx(150.0));

    const TailRisk shortRisk = positionRisk(-10, 100.0, tails);
    CHECK(shortRisk.valueAtRisk == doctest::Approx(120.0));
    CHECK(shortRisk.expectedShortfall == doctest::Approx(200.0));

    const TailRisk flat = positionRisk(0, 100.0, tails);
    CHECK(flat.valueAtRisk == 0.0);
    CHECK(flat.expectedShortfall == 0.0);
}

TEST_CASE("risk: monitor follows ticks and trades and produces a consistent report")
{
    CpuPathGenerator generator;
    RiskConfig config;
    config.paths = 100'000;
    RiskMonitor monitor(generator, config);

    // Nothing seen yet: an empty report, not a crash.
    CHECK(monitor.report().spot == 0.0);

    // Feed a price path with a known volatility through Tick messages.
    std::vector<double> steps(4000);
    generator.generate(GbmParams{1.0, 0.0, 0.0005, 1.0}, 2, steps);
    double price = 10'000.0;
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        price *= steps[i];
        agora::net::Tick tick;
        tick.tick = i + 1;
        tick.bidPrice = static_cast<agora::Price>(price) - 1;
        tick.askPrice = static_cast<agora::Price>(price) + 1;
        monitor.onTick(tick);
    }

    // Agent 1 buys 300 from agent 2, then sells 100 to agent 3.
    agora::net::Trade trade;
    trade.buyAgent = 1;
    trade.sellAgent = 2;
    trade.quantity = 300;
    monitor.onTrade(trade);
    trade.buyAgent = 3;
    trade.sellAgent = 1;
    trade.quantity = 100;
    monitor.onTrade(trade);

    const RiskReport report = monitor.report();
    CHECK(report.tick == 4000);
    CHECK(report.volatility == doctest::Approx(0.0005).epsilon(0.1));

    // Options: Monte Carlo agrees with Black-Scholes; with zero rates an
    // at-the-money call and put are worth the same.
    CHECK(std::fabs(report.call.price - report.callExact) < 4.0 * report.call.standardError);
    CHECK(std::fabs(report.put.price - report.putExact) < 4.0 * report.put.standardError);
    CHECK(report.callExact == doctest::Approx(report.putExact));
    CHECK(report.callExact > 0.0);

    // Positions net to zero and are listed in agent order.
    REQUIRE(report.agents.size() == 3);
    CHECK(report.agents[0].agent == 1);
    CHECK(report.agents[0].position == 200);
    CHECK(report.agents[1].position == -300);
    CHECK(report.agents[2].position == 100);

    // Risk scales with position size, and the simulation agrees with the closed form.
    for (const AgentRisk& agent : report.agents)
    {
        CHECK(agent.simulated.valueAtRisk > 0.0);
        CHECK(agent.simulated.expectedShortfall > agent.simulated.valueAtRisk);
        CHECK(agent.simulated.valueAtRisk == doctest::Approx(agent.analytic.valueAtRisk).epsilon(0.05));
        CHECK(agent.simulated.expectedShortfall == doctest::Approx(agent.analytic.expectedShortfall).epsilon(0.05));
    }
    CHECK(report.agents[0].simulated.valueAtRisk == doctest::Approx(2.0 * report.agents[2].simulated.valueAtRisk));
}

#ifdef AGORA_HAS_CUDA
TEST_CASE("risk: CUDA path generator passes the same checks as the CPU one")
{
    // A CUDA build may still be tested on a node without a GPU; skip rather than fail there.
    std::string reason;
    if (!CudaPathGenerator::available(reason))
    {
        MESSAGE("skipped, no usable GPU: " << reason);
        return;
    }
    CudaPathGenerator generator;
    MESSAGE("GPU: " << generator.deviceName());
    checkGenerator(generator);
}
#endif
