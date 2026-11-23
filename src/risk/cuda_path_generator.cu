/**
 * @file cuda_path_generator.cu
 * @brief CUDA kernels and host code for CudaPathGenerator.
 */
#include "agora/risk/cuda_path_generator.hpp"

#include <cuda_runtime.h>
#include <curand_kernel.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace agora::risk
{
namespace
{

/// Threads per block. 256 is a safe, efficient choice on every recent GPU.
constexpr unsigned int kThreadsPerBlock = 256;

/// Upper limit on GPU threads used by accumulate(). Each thread costs one
/// generator set-up and two doubles of output; beyond a few hundred thousand
/// threads the GPU is already saturated and more would only add overhead.
constexpr unsigned long long kMaxAccumulateThreads = 1ULL << 18;

/// Throws a std::runtime_error naming the failed call if @p status is an error.
void check(cudaError_t status, const char* what)
{
    if (status != cudaSuccess)
    {
        throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(status));
    }
}

/// Owns a device allocation and frees it on scope exit, so errors cannot leak GPU memory.
class DeviceBuffer
{
  public:
    explicit DeviceBuffer(std::size_t count)
    {
        check(cudaMalloc(&m_data, count * sizeof(double)), "cudaMalloc");
    }
    ~DeviceBuffer() { cudaFree(m_data); }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    double* get() const noexcept { return static_cast<double*>(m_data); }

  private:
    void* m_data = nullptr;
};

/**
 * @brief Each thread simulates up to @p pathsPerThread terminal prices and sums the payoff.
 *
 * Thread t handles paths [t * pathsPerThread, (t + 1) * pathsPerThread), cut
 * off at @p totalPaths, and writes its sum and sum of squares to slot t.
 *
 * @param shift, scale  S_T = spot * exp(shift + scale * Z).
 */
__global__ void accumulateKernel(double spot, double shift, double scale, double strike, int isCall,
                                 unsigned long long seed, unsigned long long pathsPerThread,
                                 unsigned long long totalPaths, double* sums, double* sumSquares)
{
    const unsigned long long thread =
        static_cast<unsigned long long>(blockIdx.x) * blockDim.x + threadIdx.x;
    const unsigned long long first = thread * pathsPerThread;

    double sum = 0.0;
    double sumSquare = 0.0;
    if (first < totalPaths)
    {
        const unsigned long long remaining = totalPaths - first;
        const unsigned long long count = remaining < pathsPerThread ? remaining : pathsPerThread;

        // Philox is counter based: (seed, thread) selects an independent stream
        // directly, with no expensive skip-ahead.
        curandStatePhilox4_32_10_t state;
        curand_init(seed, thread, 0, &state);

        for (unsigned long long i = 0; i < count; ++i)
        {
            const double price = spot * exp(shift + scale * curand_normal_double(&state));
            const double payoff = fmax(isCall ? price - strike : strike - price, 0.0);
            sum += payoff;
            sumSquare += payoff * payoff;
        }
    }
    sums[thread] = sum;
    sumSquares[thread] = sumSquare;
}

/// One thread per sample: writes the terminal price of path i to out[i].
__global__ void generateKernel(double spot, double shift, double scale, unsigned long long seed,
                               unsigned long long count, double* out)
{
    const unsigned long long index =
        static_cast<unsigned long long>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index < count)
    {
        curandStatePhilox4_32_10_t state;
        curand_init(seed, index, 0, &state);
        out[index] = spot * exp(shift + scale * curand_normal_double(&state));
    }
}

/// Number of blocks needed to cover @p threads threads.
unsigned int blocksFor(unsigned long long threads)
{
    return static_cast<unsigned int>((threads + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

} // namespace

bool CudaPathGenerator::available(std::string& reason)
{
    int deviceCount = 0;
    const cudaError_t status = cudaGetDeviceCount(&deviceCount);
    if (status != cudaSuccess)
    {
        reason = cudaGetErrorString(status);
        return false;
    }
    if (deviceCount == 0)
    {
        reason = "no CUDA device found";
        return false;
    }
    return true;
}

CudaPathGenerator::CudaPathGenerator()
{
    int deviceCount = 0;
    check(cudaGetDeviceCount(&deviceCount), "cudaGetDeviceCount");
    if (deviceCount == 0)
    {
        throw std::runtime_error("no CUDA device found");
    }
    check(cudaSetDevice(0), "cudaSetDevice");

    cudaDeviceProp properties{};
    check(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties");
    m_deviceName = properties.name;

    // Force the runtime to create its context now, so that one-time cost does
    // not land inside the first timed pricing call.
    check(cudaFree(nullptr), "cudaFree (context creation)");
}

PayoffMoments CudaPathGenerator::accumulate(const GbmParams& model, double strike, OptionType type,
                                            std::uint64_t paths, std::uint64_t seed)
{
    PayoffMoments moments;
    moments.paths = paths;
    if (paths == 0)
    {
        return moments;
    }

    const double shift = (model.drift - 0.5 * model.volatility * model.volatility) * model.horizon;
    const double scale = model.volatility * std::sqrt(model.horizon);

    // Round the thread count up to whole blocks; threads past the last path write zeros.
    const unsigned long long wanted = std::min<unsigned long long>(paths, kMaxAccumulateThreads);
    const unsigned int blocks = blocksFor(wanted);
    const unsigned long long threads = static_cast<unsigned long long>(blocks) * kThreadsPerBlock;
    const unsigned long long pathsPerThread = (paths + threads - 1) / threads;

    DeviceBuffer deviceSums(threads);
    DeviceBuffer deviceSumSquares(threads);

    accumulateKernel<<<blocks, kThreadsPerBlock>>>(model.spot, shift, scale, strike,
                                                   type == OptionType::Call ? 1 : 0, seed, pathsPerThread,
                                                   paths, deviceSums.get(), deviceSumSquares.get());
    check(cudaGetLastError(), "accumulateKernel launch");

    // The copies wait for the kernel to finish, so timing this function times the simulation.
    std::vector<double> sums(threads);
    std::vector<double> sumSquares(threads);
    check(cudaMemcpy(sums.data(), deviceSums.get(), threads * sizeof(double), cudaMemcpyDeviceToHost),
          "cudaMemcpy (sums)");
    check(cudaMemcpy(sumSquares.data(), deviceSumSquares.get(), threads * sizeof(double),
                     cudaMemcpyDeviceToHost),
          "cudaMemcpy (sum of squares)");

    for (unsigned long long i = 0; i < threads; ++i)
    {
        moments.sum += sums[i];
        moments.sumSquares += sumSquares[i];
    }
    return moments;
}

void CudaPathGenerator::generate(const GbmParams& model, std::uint64_t seed, std::vector<double>& terminal)
{
    const unsigned long long count = terminal.size();
    if (count == 0)
    {
        return;
    }
    const double shift = (model.drift - 0.5 * model.volatility * model.volatility) * model.horizon;
    const double scale = model.volatility * std::sqrt(model.horizon);

    DeviceBuffer devicePrices(count);
    generateKernel<<<blocksFor(count), kThreadsPerBlock>>>(model.spot, shift, scale, seed, count,
                                                           devicePrices.get());
    check(cudaGetLastError(), "generateKernel launch");
    check(cudaMemcpy(terminal.data(), devicePrices.get(), count * sizeof(double), cudaMemcpyDeviceToHost),
          "cudaMemcpy (terminal prices)");
}

} // namespace agora::risk
