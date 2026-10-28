/**
 * @file rng.hpp
 * @brief Portable, seedable random number generator.
 *
 * std::mt19937_64 itself is portable, but std::uniform_int_distribution and
 * friends are implementation-defined, so the same seed can give different
 * numbers under GCC and Clang. Every random draw in Agora goes through this
 * class instead, which makes "same seed, same trade log" hold on any compiler.
 */
#pragma once

#include <cstdint>

namespace agora
{

/**
 * @brief xoshiro256** generator (Blackman and Vigna), seeded with SplitMix64.
 *
 * Fast, 256 bits of state, passes BigCrush. Not cryptographic.
 */
class Rng
{
  public:
    explicit Rng(std::uint64_t seed) noexcept
    {
        // SplitMix64 expands a single 64-bit seed into the full state and
        // guarantees the state is never all zeros.
        for (std::uint64_t& word : m_state)
        {
            seed += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            word = z ^ (z >> 31);
        }
    }

    /// Next raw 64-bit value.
    std::uint64_t next() noexcept
    {
        const std::uint64_t result = rotl(m_state[1] * 5, 7) * 9;
        const std::uint64_t t = m_state[1] << 17;
        m_state[2] ^= m_state[0];
        m_state[3] ^= m_state[1];
        m_state[1] ^= m_state[2];
        m_state[0] ^= m_state[3];
        m_state[2] ^= t;
        m_state[3] = rotl(m_state[3], 45);
        return result;
    }

    /// Uniform integer in [0, bound), without modulo bias. @pre bound > 0.
    std::uint64_t below(std::uint64_t bound) noexcept
    {
        // Reject the few values that would make the modulo uneven.
        const std::uint64_t threshold = (0 - bound) % bound;
        for (;;)
        {
            const std::uint64_t r = next();
            if (r >= threshold)
            {
                return r % bound;
            }
        }
    }

    /// Uniform integer in [low, high] (inclusive). @pre low <= high.
    std::int64_t between(std::int64_t low, std::int64_t high) noexcept
    {
        const auto span = static_cast<std::uint64_t>(high - low) + 1;
        return low + static_cast<std::int64_t>(below(span));
    }

    /// Uniform double in [0, 1) with 53 bits of precision.
    double uniform() noexcept { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    /// True with probability @p p.
    bool chance(double p) noexcept { return uniform() < p; }

  private:
    static std::uint64_t rotl(std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }

    std::uint64_t m_state[4] = {};
};

} // namespace agora
