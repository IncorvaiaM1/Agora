/**
 * @file market_maker_agent.hpp
 * @brief MarketMakerAgent: quotes both sides and manages inventory.
 */
#pragma once

#include "agora/agents/agent.hpp"

#include <set>

namespace agora::agents
{

/// Parameters of the market-making strategy.
struct MarketMakerConfig
{
    Price halfSpread = 4;           ///< Quotes sit this many ticks either side of the centre price.
    Price requoteTolerance = 1;     ///< A quote this close to where it should be is left alone.
    Quantity quoteSize = 10;        ///< Shares offered on each side.
    Quantity sharesPerSkewTick = 20; ///< The centre shifts one tick against every this many shares held.
    Price maxSkew = 20;             ///< Cap on that shift.
    Quantity positionLimit = 500;   ///< Stops quoting the side that would grow the position past this.
};

/**
 * @brief Market maker.
 *
 * Keeps one bid and one ask resting around a centre price and earns the
 * spread between them. The centre is halfway between the last trade price
 * and the fundamental value (so trades move its quotes, but one freak print
 * cannot drag them arbitrarily far), shifted against the agent's inventory:
 * when it is long it lowers both quotes, so it is more likely to sell and
 * less likely to buy, and the reverse when short. When the centre moves by
 * more than a small tolerance, the old quotes are cancelled and new ones
 * placed. Market makers provide the standing liquidity that the other
 * strategies trade against.
 */
class MarketMakerAgent final : public Agent
{
  public:
    MarketMakerAgent(AgentId id, std::uint64_t seed, MarketMakerConfig config = {});

    const char* name() const noexcept override { return "mm"; }

    /// Centre price the agent would quote around for this snapshot (exposed for tests).
    Price centre(const net::Tick& tick) const noexcept;

  protected:
    void onTick(const net::Tick& tick, OrderSink& orders) override;
    void onAccepted(const net::Accepted& accepted) override;
    void onRejected(const net::Rejected& rejected) override;
    void onCancelled(const net::Cancelled& cancelled) override;

  private:
    MarketMakerConfig m_config;

    // A quote that has been sent but not yet acknowledged. Without these, a
    // slow acknowledgement (live mode) would make the agent quote twice.
    ClientOrderId m_pendingBid = 0; ///< 0 means none.
    ClientOrderId m_pendingAsk = 0;

    std::set<OrderId> m_cancelling; ///< Cancels already requested, so each is sent once.
};

} // namespace agora::agents
