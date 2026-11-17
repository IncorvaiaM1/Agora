/**
 * @file bench_latency.cpp
 * @brief agora_bench_latency: end-to-end throughput and round-trip latency (results 1 and 2).
 *
 * Usage:
 *   agora_bench_latency [--clients N] [--orders M] [--window W] [--seed S]
 *                       [--host H] [--port P] [--label TEXT] [--csv] [--csv-header]
 *
 * Starts N client threads against a running exchange (start it with
 * --tick-ms 0 so no ticks are mixed in). Each client sends M limit orders and
 * measures, for every one, the time from just before the order is sent to
 * the moment its Accepted reply has been read: a full round trip through the
 * socket, the session thread, the queue, the matching engine and back.
 *
 * Each client keeps at most W orders in flight (default 1: send, wait, send).
 * W = 1 gives the cleanest latency numbers; larger W gives peak throughput.
 *
 * To keep the book from growing without bound, a client holds at most
 * kMaxResting resting orders and cancels its oldest beyond that. Cancels are
 * part of the load but are not timed.
 *
 * --csv prints one line:
 *   label,clients,window,orders,seconds,orders_per_s,mean_us,p50_us,p99_us,p999_us,max_us
 */
#include "agora/core/rng.hpp"
#include "agora/net/client_connection.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

/// Resting orders a client keeps before it starts cancelling the oldest.
constexpr std::size_t kMaxResting = 32;

/// Orders are priced uniformly within this many ticks of kCentrePrice, so about half of them cross.
constexpr agora::Price kCentrePrice = 10'000;
constexpr agora::Price kPriceRange = 10;

struct Options
{
    std::size_t clients = 4;
    std::size_t orders = 20'000; ///< Per client.
    std::size_t window = 1;
    std::uint64_t seed = 42;
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    std::string label = "run";
    bool csv = false;
    bool csvHeader = false;
};

void printUsage()
{
    std::cerr << "usage: agora_bench_latency [--clients N] [--orders M] [--window W] [--seed S]\n"
                 "                           [--host H] [--port P] [--label TEXT] [--csv] [--csv-header]\n";
}

bool parse(int argc, char* argv[], Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--clients" && hasValue)
        {
            options.clients = std::stoull(argv[++i]);
        }
        else if (arg == "--orders" && hasValue)
        {
            options.orders = std::stoull(argv[++i]);
        }
        else if (arg == "--window" && hasValue)
        {
            options.window = std::stoull(argv[++i]);
        }
        else if (arg == "--seed" && hasValue)
        {
            options.seed = std::stoull(argv[++i]);
        }
        else if (arg == "--host" && hasValue)
        {
            options.host = argv[++i];
        }
        else if (arg == "--port" && hasValue)
        {
            options.port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--label" && hasValue)
        {
            options.label = argv[++i];
        }
        else if (arg == "--csv")
        {
            options.csv = true;
        }
        else if (arg == "--csv-header")
        {
            options.csvHeader = true;
        }
        else
        {
            return false;
        }
    }
    return options.clients > 0 && options.orders > 0 && options.window > 0;
}

/// One benchmark client: its connection, its workload and its measurements.
class BenchClient
{
  public:
    BenchClient(const Options& options, agora::AgentId id)
        : m_connection(options.host, options.port), m_rng(options.seed + id), m_id(id),
          m_orders(options.orders), m_window(options.window)
    {
        m_latenciesNanos.reserve(options.orders);
    }

    /// Handshake. Done for every client before the clock starts.
    void connect()
    {
        agora::net::Hello hello;
        hello.agent = m_id;
        m_connection.send(hello);
        m_connection.flush();
        agora::net::ServerMessage message;
        if (!m_connection.receive(message) || !std::holds_alternative<agora::net::Welcome>(message) ||
            std::get<agora::net::Welcome>(message).status != agora::net::WelcomeStatus::Ok)
        {
            throw std::runtime_error("bench client " + std::to_string(m_id) + ": handshake failed");
        }
    }

    /// Sends all orders and collects one latency sample per order.
    void run()
    {
        std::size_t sent = 0;
        std::size_t answered = 0;
        while (answered < m_orders)
        {
            // Top the window up, then send the whole batch with one write.
            while (sent < m_orders && sent - answered < m_window)
            {
                agora::net::NewOrder order;
                order.clientOrderId = sent + 1;
                order.side = m_rng.chance(0.5) ? agora::Side::Buy : agora::Side::Sell;
                order.orderType = agora::OrderType::Limit;
                order.price = kCentrePrice + m_rng.between(-kPriceRange, kPriceRange);
                order.quantity = m_rng.between(1, 10);
                m_sendTimes[order.clientOrderId] = Clock::now();
                m_connection.send(order);
                ++sent;
            }
            if (!m_connection.flush())
            {
                throw std::runtime_error("bench client: connection lost while sending");
            }

            // Read until one of our orders is answered (other messages are trades and cancel replies).
            bool gotReply = false;
            while (!gotReply)
            {
                agora::net::ServerMessage message;
                if (!m_connection.receive(message))
                {
                    throw std::runtime_error("bench client: connection lost while receiving");
                }
                if (const auto* accepted = std::get_if<agora::net::Accepted>(&message))
                {
                    record(accepted->clientOrderId);
                    gotReply = true;
                    m_resting.push_back(accepted->orderId);
                    if (m_resting.size() > kMaxResting)
                    {
                        // Untimed housekeeping. It may be rejected if the order already filled.
                        m_connection.send(agora::net::CancelOrder{0, m_resting.front()});
                        m_resting.pop_front();
                    }
                }
                else if (const auto* rejected = std::get_if<agora::net::Rejected>(&message))
                {
                    // Client order id 0 marks our own cancels; anything else is a timed order.
                    if (rejected->clientOrderId != 0)
                    {
                        record(rejected->clientOrderId);
                        gotReply = true;
                    }
                }
            }
            ++answered;
        }
        m_connection.flush();
    }

    const std::vector<std::int64_t>& latenciesNanos() const noexcept { return m_latenciesNanos; }

  private:
    void record(agora::ClientOrderId clientOrderId)
    {
        const auto found = m_sendTimes.find(clientOrderId);
        if (found != m_sendTimes.end())
        {
            m_latenciesNanos.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - found->second).count());
            m_sendTimes.erase(found);
        }
    }

    agora::net::ClientConnection m_connection;
    agora::Rng m_rng;
    agora::AgentId m_id;
    std::size_t m_orders;
    std::size_t m_window;
    std::unordered_map<agora::ClientOrderId, Clock::time_point> m_sendTimes; ///< Orders in flight.
    std::deque<agora::OrderId> m_resting;
    std::vector<std::int64_t> m_latenciesNanos;
};

/// Value at quantile @p q (0..1) of a sorted sample, in microseconds.
double percentileMicros(const std::vector<std::int64_t>& sorted, double q)
{
    const auto index = static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[index]) / 1000.0;
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    try
    {
        if (!parse(argc, argv, options))
        {
            printUsage();
            return 2;
        }
    }
    catch (const std::exception&)
    {
        printUsage(); // A number failed to parse.
        return 2;
    }

    try
    {
        // Connect everyone first so connection setup is not part of the measurement.
        std::vector<std::unique_ptr<BenchClient>> clients;
        for (std::size_t i = 0; i < options.clients; ++i)
        {
            clients.push_back(std::make_unique<BenchClient>(options, static_cast<agora::AgentId>(i + 1)));
            clients.back()->connect();
        }

        std::atomic<bool> go{false};
        std::atomic<bool> failed{false};
        std::vector<std::thread> threads;
        for (const std::unique_ptr<BenchClient>& client : clients)
        {
            threads.emplace_back(
                [&client, &go, &failed]
                {
                    // Start line: all threads begin sending together.
                    while (!go.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                    try
                    {
                        client->run();
                    }
                    catch (const std::exception& error)
                    {
                        std::cerr << "error: " << error.what() << '\n';
                        failed.store(true);
                    }
                });
        }

        const Clock::time_point start = Clock::now();
        go.store(true, std::memory_order_release);
        for (std::thread& thread : threads)
        {
            thread.join();
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        if (failed.load())
        {
            return 1;
        }

        std::vector<std::int64_t> all;
        for (const std::unique_ptr<BenchClient>& client : clients)
        {
            all.insert(all.end(), client->latenciesNanos().begin(), client->latenciesNanos().end());
        }
        std::sort(all.begin(), all.end());
        double sumNanos = 0.0;
        for (const std::int64_t sample : all)
        {
            sumNanos += static_cast<double>(sample);
        }

        const double throughput = static_cast<double>(all.size()) / seconds;
        const double meanMicros = sumNanos / static_cast<double>(all.size()) / 1000.0;
        const double p50 = percentileMicros(all, 0.50);
        const double p99 = percentileMicros(all, 0.99);
        const double p999 = percentileMicros(all, 0.999);
        const double maxMicros = static_cast<double>(all.back()) / 1000.0;

        std::cout << std::fixed;
        if (options.csvHeader)
        {
            std::cout << "label,clients,window,orders,seconds,orders_per_s,mean_us,p50_us,p99_us,p999_us,max_us\n";
        }
        if (options.csv)
        {
            std::cout << options.label << ',' << options.clients << ',' << options.window << ',' << all.size()
                      << ',' << std::setprecision(4) << seconds << ',' << std::setprecision(0) << throughput
                      << std::setprecision(1) << ',' << meanMicros << ',' << p50 << ',' << p99 << ',' << p999
                      << ',' << maxMicros << '\n';
        }
        else
        {
            std::cout << options.label << ": " << options.clients << " clients, window " << options.window << ", "
                      << all.size() << " orders in " << std::setprecision(3) << seconds << " s\n"
                      << "  throughput  " << std::setprecision(0) << throughput << " orders/s\n"
                      << std::setprecision(1) << "  round trip  mean " << meanMicros << " us, p50 " << p50
                      << " us, p99 " << p99 << " us, p99.9 " << p999 << " us, max " << maxMicros << " us\n";
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
