/**
 * @file agent_factory.cpp
 * @brief Implementation of makeAgent().
 */
#include "agora/agents/agent_factory.hpp"

#include "agora/agents/market_maker_agent.hpp"
#include "agora/agents/trend_agent.hpp"
#include "agora/agents/value_agent.hpp"
#include "agora/agents/zero_intelligence_agent.hpp"

namespace agora::agents
{

std::unique_ptr<Agent> makeAgent(const std::string& type, AgentId id, std::uint64_t seed)
{
    if (type == "zi")
    {
        return std::make_unique<ZeroIntelligenceAgent>(id, seed);
    }
    if (type == "value")
    {
        return std::make_unique<ValueAgent>(id, seed);
    }
    if (type == "trend")
    {
        return std::make_unique<TrendAgent>(id, seed);
    }
    if (type == "mm")
    {
        return std::make_unique<MarketMakerAgent>(id, seed);
    }
    return nullptr;
}

} // namespace agora::agents
