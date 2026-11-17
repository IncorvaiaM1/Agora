/**
 * @file agent_factory.hpp
 * @brief Builds an Agent from its strategy name.
 */
#pragma once

#include "agora/agents/agent.hpp"

#include <memory>
#include <string>

namespace agora::agents
{

/// Strategy names accepted by makeAgent(), for usage messages.
constexpr const char* kAgentTypes = "zi|value|trend|mm";

/**
 * @brief Creates a strategy with its default parameters.
 * @param type "zi", "value", "trend" or "mm".
 * @return The agent, or nullptr if @p type is not a known strategy.
 */
std::unique_ptr<Agent> makeAgent(const std::string& type, AgentId id, std::uint64_t seed);

} // namespace agora::agents
