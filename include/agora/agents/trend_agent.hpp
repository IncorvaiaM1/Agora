/**
 * @file trend_agent.hpp
 * @brief TrendAgent: a momentum trader driven by a moving-average crossover.
 */
#pragma once

#include "agora/agents/agent.hpp"

namespace agora::agents
{

/// Parameters of the trend strategy.
struct TrendConfig
{
    double actProbability = 0.5;   ///< Chance of acting on a signal in a given tick.
    std::int64_t minFastWindow = 4;  ///< Each agent draws its fast window from [minFastWindow, maxFastWindow]...
    std::int64_t maxFastWindow = 12;
    std::int64_t minSlowWindow = 30; ///< ... and its slow window from [minSlowWindow, maxSlowWindow].
    std::int64_t maxSlowWindow = 90;
    Price threshold = 2;           ///< The fast average must lead the slow one by more than this many ticks.
    Price maxSlippage = 5;         ///< Pays at most this many ticks through the best quote.
    Quantity maxQuantity = 10;     ///< Largest single order.
    Quantity positionLimit = 200;  ///< Stops adding to a position beyond this many shares.
};

/**
 * @brief Trend follower (chartist).
 *
 * Keeps a fast and a slow exponential moving average of the price. When the
 * fast one is above the slow one by more than a threshold the price has been
 * rising, and the agent buys; in the opposite case it sells. Bigger gaps
 * give bigger orders. Orders are limit orders priced a few ticks through the
 * best quote, so they trade at once like market orders but cannot sweep a
 * thin book to an absurd price; whatever is left unfilled is cancelled on
 * the next tick. Buying into rises and selling into falls feeds
 * back on the price, which is the mechanism behind fat tails and volatility
 * clustering in agent-based markets (result 3).
 *
 * The averages use integer fixed-point arithmetic, not floating point, so
 * the agent behaves identically under every compiler. The windows are drawn
 * per agent so a group of trend followers does not act as one.
 */
class TrendAgent final : public Agent
{
  public:
    TrendAgent(AgentId id, std::uint64_t seed, TrendConfig config = {});

    const char* name() const noexcept override { return "trend"; }

    std::int64_t fastWindow() const noexcept { return m_fastWindow; }
    std::int64_t slowWindow() const noexcept { return m_slowWindow; }

    /// Fast average minus slow average, in whole price ticks (exposed for tests).
    Price signal() const noexcept { return (m_fast - m_slow) / kScale; }

  protected:
    void onTick(const net::Tick& tick, OrderSink& orders) override;

  private:
    /// Averages are stored as price * kScale to keep fractional precision in integers.
    static constexpr std::int64_t kScale = 1024;

    TrendConfig m_config;
    std::int64_t m_fastWindow;
    std::int64_t m_slowWindow;
    std::int64_t m_fast = 0;
    std::int64_t m_slow = 0;
    std::int64_t m_samples = 0; ///< Ticks observed so far, for the warm-up.
};

} // namespace agora::agents
