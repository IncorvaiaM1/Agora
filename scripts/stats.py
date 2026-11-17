#!/usr/bin/env python3
"""Market statistics for result 3: fat tails and volatility clustering.

Reads the tick logs written by agora_exchange (--tick-log) and measures, for
each market and seed:

  * excess kurtosis of returns          (0 for a normal distribution; > 0 means fat tails)
  * autocorrelation of returns          (near 0 in any market that is hard to predict)
  * autocorrelation of squared returns  (0 for a random walk; > 0 and slowly decaying
                                         means volatility clusters: big moves follow big moves)

and compares them with a random-walk baseline: a simulated price whose returns
are independent normal draws with the same volatility and length as the first
market's.

Expected input layout (this is what scripts/stylized_facts.sh produces):

    <root>/<market>/seed_<n>/ticks.csv

Outputs, all small CSV files that are committed with the report:

    <out>/stylized_facts.csv    one row per market and seed
    <out>/stylized_summary.csv  mean and standard deviation over seeds
    <out>/acf_squared.csv       autocorrelation of squared returns, lags 1..max
    <out>/returns_hist.csv      histogram of standardized returns, pooled over seeds
    <out>/price_path.csv        price and fundamental value for one seed per market

Only the Python standard library is used, so this runs anywhere.

Usage: stats.py --root logs/result3 --out results [--bar 10] [--max-lag 50]
"""
import argparse
import csv
import math
import os
import random
import sys

# Lags reported as columns in stylized_facts.csv (the full curve goes to acf_squared.csv).
REPORT_LAGS = (1, 2, 5, 10, 20, 50)

# Histogram of standardized returns: bins of this width from -HIST_RANGE to +HIST_RANGE.
HIST_RANGE = 8.0
HIST_BIN = 0.5

BASELINE = "random_walk"


def read_prices(path):
    """Returns (ticks, prices, fundamentals) from a tick log.

    The price is the mid of the best bid and ask. Ticks before both sides are
    quoted fall back to the last trade price, and are skipped if there is none.
    """
    ticks, prices, fundamentals = [], [], []
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            bid = int(row["bid_price"])
            ask = int(row["ask_price"])
            last = int(row["last_trade_price"])
            if bid > 0 and ask > 0:
                price = (bid + ask) / 2.0
            elif last > 0:
                price = float(last)
            else:
                continue
            ticks.append(int(row["tick"]))
            prices.append(price)
            fundamentals.append(int(row["fundamental"]))
    return ticks, prices, fundamentals


def log_returns(prices, bar):
    """Log returns between prices sampled every `bar` ticks."""
    sampled = prices[::bar]
    return [math.log(sampled[i + 1] / sampled[i]) for i in range(len(sampled) - 1)]


def mean(values):
    return sum(values) / len(values)


def std(values):
    """Population standard deviation."""
    centre = mean(values)
    return math.sqrt(sum((v - centre) ** 2 for v in values) / len(values))


def excess_kurtosis(values):
    """Fourth standardized moment minus 3."""
    centre = mean(values)
    variance = sum((v - centre) ** 2 for v in values) / len(values)
    if variance == 0.0:
        return 0.0
    fourth = sum((v - centre) ** 4 for v in values) / len(values)
    return fourth / variance**2 - 3.0


def autocorrelation(values, max_lag):
    """Sample autocorrelation at lags 1..max_lag (list index 0 is lag 1)."""
    centre = mean(values)
    deviations = [v - centre for v in values]
    denominator = sum(d * d for d in deviations)
    if denominator == 0.0:
        return [0.0] * max_lag
    count = len(deviations)
    return [
        sum(deviations[i] * deviations[i + lag] for i in range(count - lag)) / denominator
        for lag in range(1, max_lag + 1)
    ]


def random_walk_prices(count, start, tick_volatility, seed):
    """A price path with independent normal log returns, rounded to whole ticks like the exchange's."""
    generator = random.Random(seed)
    prices = [float(start)]
    log_price = math.log(start)
    for _ in range(count - 1):
        log_price += generator.gauss(0.0, tick_volatility)
        prices.append(float(max(1, round(math.exp(log_price)))))
    return prices


def measure(prices, bar, max_lag):
    """All statistics for one price path."""
    returns = log_returns(prices, bar)
    if len(returns) <= max_lag + 1:
        raise ValueError("too few returns (%d) for max lag %d; run more ticks" % (len(returns), max_lag))
    squared = [r * r for r in returns]
    return {
        "returns": returns,
        "bars": len(returns),
        "return_std": std(returns),
        "excess_kurtosis": excess_kurtosis(returns),
        "acf_returns": autocorrelation(returns, max_lag),
        "acf_squared": autocorrelation(squared, max_lag),
    }


def discover(root):
    """Returns {market: {seed: path to ticks.csv}} for everything under root."""
    found = {}
    for market in sorted(os.listdir(root)):
        market_dir = os.path.join(root, market)
        if not os.path.isdir(market_dir):
            continue
        for run in sorted(os.listdir(market_dir)):
            path = os.path.join(market_dir, run, "ticks.csv")
            if run.startswith("seed_") and os.path.isfile(path):
                found.setdefault(market, {})[int(run[len("seed_"):])] = path
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", required=True, help="directory holding <market>/seed_<n>/ticks.csv")
    parser.add_argument("--out", default="results", help="directory for the output CSV files")
    parser.add_argument("--bar", type=int, default=10, help="ticks per return (default 10)")
    parser.add_argument("--max-lag", type=int, default=50, help="largest autocorrelation lag (default 50)")
    args = parser.parse_args()

    runs = discover(args.root)
    if not runs:
        sys.exit("no <market>/seed_<n>/ticks.csv found under " + args.root)
    os.makedirs(args.out, exist_ok=True)
    report_lags = [lag for lag in REPORT_LAGS if lag <= args.max_lag]

    results = {}  # market -> seed -> statistics
    paths = {}    # market -> (ticks, prices, fundamentals) of its first seed
    for market, seeds in runs.items():
        for seed, path in sorted(seeds.items()):
            ticks, prices, fundamentals = read_prices(path)
            results.setdefault(market, {})[seed] = measure(prices, args.bar, args.max_lag)
            paths.setdefault(market, (ticks, prices, fundamentals))

    # Baseline: one random walk per seed of the first market, matched in length,
    # starting price and per-tick volatility.
    reference = next(iter(runs))
    for seed, path in sorted(runs[reference].items()):
        _, prices, _ = read_prices(path)
        # Per-tick volatility implied by the bar returns, so the two series match at the bar scale.
        tick_volatility = results[reference][seed]["return_std"] / math.sqrt(args.bar)
        walk = random_walk_prices(len(prices), prices[0], tick_volatility, seed)
        results.setdefault(BASELINE, {})[seed] = measure(walk, args.bar, args.max_lag)

    # --- one row per market and seed ---------------------------------------
    with open(os.path.join(args.out, "stylized_facts.csv"), "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["market", "seed", "bars", "return_std", "excess_kurtosis", "acf_returns_1"]
                        + ["acf_squared_%d" % lag for lag in report_lags])
        for market, seeds in results.items():
            for seed, stats in sorted(seeds.items()):
                writer.writerow([market, seed, stats["bars"], "%.6g" % stats["return_std"],
                                 "%.4f" % stats["excess_kurtosis"], "%.4f" % stats["acf_returns"][0]]
                                + ["%.4f" % stats["acf_squared"][lag - 1] for lag in report_lags])

    # --- mean and spread over seeds ------------------------------------------
    def over_seeds(market, pick):
        values = [pick(stats) for stats in results[market].values()]
        return mean(values), std(values)

    metrics = [("excess_kurtosis", lambda s: s["excess_kurtosis"]),
               ("acf_returns_1", lambda s: s["acf_returns"][0])]
    for lag in report_lags:
        metrics.append(("acf_squared_%d" % lag, lambda s, lag=lag: s["acf_squared"][lag - 1]))

    with open(os.path.join(args.out, "stylized_summary.csv"), "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["market", "metric", "mean", "std", "seeds"])
        for market in results:
            for name, pick in metrics:
                centre, spread = over_seeds(market, pick)
                writer.writerow([market, name, "%.4f" % centre, "%.4f" % spread, len(results[market])])

    # --- full autocorrelation curve of squared returns ------------------------
    with open(os.path.join(args.out, "acf_squared.csv"), "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["market", "lag", "mean", "std"])
        for market in results:
            for lag in range(1, args.max_lag + 1):
                centre, spread = over_seeds(market, lambda s, lag=lag: s["acf_squared"][lag - 1])
                writer.writerow([market, lag, "%.5f" % centre, "%.5f" % spread])

    # --- histogram of standardized returns, pooled over seeds -----------------
    bins = int(round(2 * HIST_RANGE / HIST_BIN))
    with open(os.path.join(args.out, "returns_hist.csv"), "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["market", "bin_centre", "density"])
        for market, seeds in results.items():
            counts = [0] * bins
            total = 0
            for stats in seeds.values():
                centre, spread = mean(stats["returns"]), stats["return_std"]
                for value in stats["returns"]:
                    z = (value - centre) / spread if spread > 0 else 0.0
                    index = int((z + HIST_RANGE) / HIST_BIN)
                    counts[min(max(index, 0), bins - 1)] += 1  # the end bins collect the extremes
                    total += 1
            for index, count in enumerate(counts):
                writer.writerow([market, "%.2f" % (-HIST_RANGE + (index + 0.5) * HIST_BIN),
                                 "%.6g" % (count / (total * HIST_BIN))])

    # --- one example price path per market ------------------------------------
    with open(os.path.join(args.out, "price_path.csv"), "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["market", "tick", "price", "fundamental"])
        for market, (ticks, prices, fundamentals) in paths.items():
            for index in range(0, len(ticks), args.bar):
                writer.writerow([market, ticks[index], "%.1f" % prices[index], fundamentals[index]])

    # --- summary on the terminal ------------------------------------------------
    print("returns over %d-tick bars; mean (std) over seeds" % args.bar)
    header = "%-18s %5s %18s %16s" % ("market", "seeds", "excess kurtosis", "ACF returns 1")
    for lag in report_lags:
        header += " %16s" % ("ACF sq. lag %d" % lag)
    print(header)
    for market in results:
        line = "%-18s %5d" % (market, len(results[market]))
        for index, (_, pick) in enumerate(metrics):
            centre, spread = over_seeds(market, pick)
            line += (" %10.2f (%5.2f)" if index == 0 else " %8.3f (%5.3f)") % (centre, spread)
        print(line)
    print("wrote CSV files to " + args.out)


if __name__ == "__main__":
    main()
