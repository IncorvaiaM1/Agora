/**
 * @file cuda_path_generator.hpp
 * @brief CudaPathGenerator: the GPU implementation of PathGenerator.
 *
 * This header contains no CUDA types, so ordinary C++ files can include it.
 * The implementation (cuda_path_generator.cu) is only compiled when CMake
 * finds a CUDA compiler; code that uses this class must be guarded with
 * `#ifdef AGORA_HAS_CUDA`.
 */
#pragma once

#include "agora/risk/path_generator.hpp"

#include <string>

namespace agora::risk
{

/**
 * @brief Simulates geometric Brownian motion on an NVIDIA GPU.
 *
 * Every GPU thread owns an independent random stream (cuRAND's Philox
 * counter-based generator, keyed by the seed and the thread index), so
 * threads never share generator state and the result does not depend on how
 * the GPU schedules them.
 *
 *  - accumulate(): each thread simulates a batch of paths and keeps its own
 *    running payoff sums; only one pair of numbers per thread comes back to
 *    the host. Memory use is independent of the path count.
 *  - generate(): one thread per sample writes the terminal price to a
 *    device buffer, which is copied back.
 */
class CudaPathGenerator final : public PathGenerator
{
  public:
    /// Selects GPU 0 and initializes the CUDA runtime. @throws std::runtime_error if no GPU is usable.
    CudaPathGenerator();

    const char* name() const noexcept override { return "cuda"; }
    void generate(const GbmParams& model, std::uint64_t seed, std::vector<double>& terminal) override;
    PayoffMoments accumulate(const GbmParams& model, double strike, OptionType type, std::uint64_t paths,
                             std::uint64_t seed) override;

    /**
     * @brief Checks whether a usable GPU and driver are present, without throwing.
     * @param reason Set to the CUDA error text when the answer is no.
     */
    static bool available(std::string& reason);

    /// Name of the GPU in use, e.g. "NVIDIA H100 80GB HBM3".
    const std::string& deviceName() const noexcept { return m_deviceName; }

  private:
    std::string m_deviceName;
};

} // namespace agora::risk
