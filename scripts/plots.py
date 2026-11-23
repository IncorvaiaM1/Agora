#!/usr/bin/env python3
"""Draws the report figures from the CSV files in results/.

    latency_throughput.png    result 1 and 2: orders per second vs clients, both queues
    latency_percentiles.png   result 1 and 2: p50 / p99 / p99.9 round trip vs clients
    stylized_returns.png      result 3: distribution of returns against the normal curve
    stylized_acf.png          result 3: autocorrelation of squared returns
    price_path.png            result 3: price against the fundamental value, one seed
    risk_convergence.png      result 4: Monte Carlo error vs paths, and run time vs paths (CPU and GPU)
    risk_live.png             result 4: live option price and value at risk, simulated vs closed form

A figure is skipped, with a message, if its input file is missing, so this can
be run after only some of the experiments. Needs matplotlib.

Usage: plots.py [--results results]
"""
import argparse
import csv
import math
import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")  # no display needed (PACE-ICE nodes have none)
import matplotlib.pyplot as plt

# One fixed colour, marker and line style per series, so a series looks the
# same in every figure and can still be told apart in a greyscale printout.
STYLE = {
    "spsc": {"color": "#2a78d6", "marker": "o", "linestyle": "-", "label": "Lock-free SPSC ring"},
    "mutex": {"color": "#eb6834", "marker": "s", "linestyle": "--", "label": "Mutex queue"},
    "mixed": {"color": "#2a78d6", "marker": "o", "linestyle": "-", "label": "Mixed agents"},
    "zero_intelligence": {"color": "#eb6834", "marker": "s", "linestyle": "--", "label": "Zero intelligence only"},
    "random_walk": {"color": "#1baf7a", "marker": "^", "linestyle": ":", "label": "Random walk"},
    "cpu": {"color": "#eb6834", "marker": "s", "linestyle": "--", "label": "CPU, one thread"},
    "cuda": {"color": "#2a78d6", "marker": "o", "linestyle": "-", "label": "GPU (CUDA)"},
}
INK = "#2b2b2a"
MUTED = "#8a8980"
GRID = "#e4e3dd"


def style_axes(axis):
    """Quiet frame and grid so the data carries the figure."""
    axis.grid(True, color=GRID, linewidth=0.8)
    axis.set_axisbelow(True)
    for side in ("top", "right"):
        axis.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        axis.spines[side].set_color(MUTED)
    axis.tick_params(colors=INK)


def series_style(name):
    return STYLE.get(name, {"color": MUTED, "marker": "x", "linestyle": "-", "label": name})


def read_rows(path):
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


def mean_std(values):
    centre = sum(values) / len(values)
    return centre, math.sqrt(sum((v - centre) ** 2 for v in values) / len(values))


def median_range(values):
    """Median, and how far the smallest and largest values lie below and above it.

    The median is used for timings because one repeat disturbed by another job
    on the node would drag a mean a long way.
    """
    ordered = sorted(values)
    middle = len(ordered) // 2
    median = ordered[middle] if len(ordered) % 2 else (ordered[middle - 1] + ordered[middle]) / 2.0
    return median, median - ordered[0], ordered[-1] - median


def save(figure, directory, name):
    path = os.path.join(directory, name)
    figure.savefig(path, dpi=200, bbox_inches="tight")
    plt.close(figure)
    print("wrote " + path)


def plot_latency(results):
    path = os.path.join(results, "latency.csv")
    if not os.path.isfile(path):
        print("skipping latency figures: no " + path)
        return
    grouped = defaultdict(lambda: defaultdict(list))  # queue -> clients -> rows
    for row in read_rows(path):
        grouped[row["queue"]][int(row["clients"])].append(row)
    client_counts = sorted({clients for by_clients in grouped.values() for clients in by_clients})

    # Throughput.
    figure, axis = plt.subplots(figsize=(6.0, 4.0))
    for queue, by_clients in grouped.items():
        look = series_style(queue)
        xs = sorted(by_clients)
        stats = [median_range([float(r["orders_per_s"]) for r in by_clients[x]]) for x in xs]
        axis.errorbar(xs, [s[0] for s in stats], yerr=[[s[1] for s in stats], [s[2] for s in stats]],
                      color=look["color"],
                      marker=look["marker"], linestyle=look["linestyle"], linewidth=2, markersize=6,
                      capsize=3, label=look["label"])
    axis.set_xscale("log", base=2)
    axis.set_xticks(client_counts)
    axis.set_xticklabels([str(c) for c in client_counts])
    axis.set_ylim(bottom=0)
    axis.set_xlabel("Connected clients", color=INK)
    axis.set_ylabel("Orders per second", color=INK)
    axis.set_title("Exchange throughput (median of repeats, bars show the full range)",
                   color=INK, fontsize=10, loc="left")
    axis.legend(frameon=False)
    style_axes(axis)
    save(figure, results, "latency_throughput.png")

    # Latency percentiles: one panel per percentile, shared log axis, so the
    # two queues are compared like for like.
    columns = [("p50_us", "Median (p50)"), ("p99_us", "p99"), ("p999_us", "p99.9")]
    figure, axes = plt.subplots(1, 3, figsize=(10.5, 3.6), sharey=True)
    for axis, (column, title) in zip(axes, columns):
        for queue, by_clients in grouped.items():
            look = series_style(queue)
            xs = sorted(by_clients)
            stats = [median_range([float(r[column]) for r in by_clients[x]]) for x in xs]
            axis.errorbar(xs, [s[0] for s in stats], yerr=[[s[1] for s in stats], [s[2] for s in stats]],
                          color=look["color"],
                          marker=look["marker"], linestyle=look["linestyle"], linewidth=2, markersize=6,
                          capsize=3, label=look["label"])
        axis.set_xscale("log", base=2)
        axis.set_yscale("log")
        axis.set_xticks(client_counts)
        axis.set_xticklabels([str(c) for c in client_counts])
        axis.set_title(title, color=INK, fontsize=10, loc="left")
        axis.set_xlabel("Connected clients", color=INK)
        style_axes(axis)
    axes[0].set_ylabel("Round-trip latency (microseconds, log scale)", color=INK)
    figure.suptitle("Median of repeats, bars show the full range", color=INK, fontsize=9, x=0.125, ha="left")
    axes[0].legend(frameon=False)
    save(figure, results, "latency_percentiles.png")


def plot_returns(results):
    path = os.path.join(results, "returns_hist.csv")
    if not os.path.isfile(path):
        print("skipping return distribution: no " + path)
        return
    grouped = defaultdict(list)
    for row in read_rows(path):
        if float(row["density"]) > 0:
            grouped[row["market"]].append((float(row["bin_centre"]), float(row["density"])))

    figure, axis = plt.subplots(figsize=(6.0, 4.0))
    zs = [i / 20.0 for i in range(-160, 161)]
    axis.plot(zs, [math.exp(-z * z / 2) / math.sqrt(2 * math.pi) for z in zs], color=MUTED, linewidth=1.5,
              label="Normal distribution")
    for market, points in grouped.items():
        look = series_style(market)
        axis.plot([p[0] for p in points], [p[1] for p in points], color=look["color"], marker=look["marker"],
                  linestyle="none", markersize=5, label=look["label"])
    axis.set_yscale("log")
    axis.set_ylim(bottom=1e-5)
    axis.set_xlabel("Return, in standard deviations", color=INK)
    axis.set_ylabel("Probability density (log scale)", color=INK)
    axis.set_title("Distribution of returns: points above the curve in the tails are fat tails",
                   color=INK, fontsize=10, loc="left")
    axis.legend(frameon=False, loc="upper right", fontsize=8)
    style_axes(axis)
    save(figure, results, "stylized_returns.png")


def plot_acf(results):
    path = os.path.join(results, "acf_squared.csv")
    if not os.path.isfile(path):
        print("skipping autocorrelation figure: no " + path)
        return
    grouped = defaultdict(list)
    for row in read_rows(path):
        grouped[row["market"]].append((int(row["lag"]), float(row["mean"]), float(row["std"])))

    figure, axis = plt.subplots(figsize=(6.0, 4.0))
    axis.axhline(0.0, color=MUTED, linewidth=1)
    for market, points in grouped.items():
        look = series_style(market)
        lags = [p[0] for p in points]
        centre = [p[1] for p in points]
        spread = [p[2] for p in points]
        axis.fill_between(lags, [c - s for c, s in zip(centre, spread)], [c + s for c, s in zip(centre, spread)],
                          color=look["color"], alpha=0.15, linewidth=0)
        axis.plot(lags, centre, color=look["color"], linestyle=look["linestyle"], linewidth=2,
                  label=look["label"])
    axis.set_xlabel("Lag (bars)", color=INK)
    axis.set_ylabel("Autocorrelation of squared returns", color=INK)
    axis.set_title("Volatility clustering (mean over seeds, band shows one standard deviation)",
                   color=INK, fontsize=10, loc="left")
    axis.legend(frameon=False)
    style_axes(axis)
    save(figure, results, "stylized_acf.png")


def plot_price(results):
    path = os.path.join(results, "price_path.csv")
    if not os.path.isfile(path):
        print("skipping price path: no " + path)
        return
    grouped = defaultdict(list)
    for row in read_rows(path):
        grouped[row["market"]].append((int(row["tick"]), float(row["price"]), int(row["fundamental"])))

    # One panel per market, same axes: both are prices in ticks.
    figure, axes = plt.subplots(len(grouped), 1, figsize=(8.0, 2.8 * len(grouped)), sharex=True, squeeze=False)
    for axis, (market, points) in zip(axes[:, 0], grouped.items()):
        look = series_style(market)
        ticks = [p[0] for p in points]
        axis.plot(ticks, [p[2] for p in points], color=MUTED, linewidth=1.5, label="Fundamental value")
        axis.plot(ticks, [p[1] for p in points], color=look["color"], linewidth=1.2, label="Mid price")
        axis.set_title(look["label"], color=INK, fontsize=10, loc="left")
        axis.set_ylabel("Price (ticks)", color=INK)
        # Above the plot area, so the legend can never cover the price lines.
        axis.legend(frameon=False, loc="lower right", bbox_to_anchor=(1.0, 1.0), ncol=2, fontsize=8)
        style_axes(axis)
    axes[-1, 0].set_xlabel("Simulation tick", color=INK)
    save(figure, results, "price_path.png")


def plot_convergence(results):
    path = os.path.join(results, "convergence.csv")
    if not os.path.isfile(path):
        print("skipping convergence figure: no " + path)
        return
    rows = read_rows(path)
    # backend -> option -> paths -> list of rows (one per seed)
    grouped = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    devices = {}
    for row in rows:
        grouped[row["backend"]][row["option"]][int(row["paths"])].append(row)
        devices[row["backend"]] = row["device"]

    figure, (left, right) = plt.subplots(1, 2, figsize=(11.0, 4.0))

    # Left: root-mean-square error over seeds, against paths. The call only, so
    # the panel stays readable; the put behaves the same (see the CSV).
    for backend, by_option in grouped.items():
        look = series_style(backend)
        by_paths = by_option["call"]
        xs = sorted(by_paths)
        rms = [math.sqrt(sum(float(r["error"]) ** 2 for r in by_paths[x]) / len(by_paths[x])) for x in xs]
        left.plot(xs, rms, color=look["color"], marker=look["marker"], linestyle="none", markersize=7,
                  label=look["label"] + " (RMS error over seeds)")
    # Reference: the mean reported standard error, which falls exactly as 1/sqrt(N).
    reference = next(iter(grouped.values()))["call"]
    xs = sorted(reference)
    standard = [sum(float(r["std_error"]) for r in reference[x]) / len(reference[x]) for x in xs]
    left.plot(xs, standard, color=MUTED, linewidth=1.5, label="Reported standard error, slope -1/2")
    left.set_xscale("log")
    left.set_yscale("log")
    left.set_xlabel("Simulated paths N", color=INK)
    left.set_ylabel("Error vs Black-Scholes (log scale)", color=INK)
    left.set_title("European call: error falls as 1/sqrt(N)", color=INK, fontsize=10, loc="left")
    left.legend(frameon=False, fontsize=8)
    style_axes(left)

    # Right: median run time per estimate, against paths, both options pooled.
    medians = {}
    for backend, by_option in grouped.items():
        look = series_style(backend)
        pooled = defaultdict(list)
        for by_paths in by_option.values():
            for paths, entries in by_paths.items():
                pooled[paths].extend(float(r["seconds"]) for r in entries)
        xs = sorted(pooled)
        ys = [median_range(pooled[x])[0] for x in xs]
        medians[backend] = dict(zip(xs, ys))
        right.plot(xs, ys, color=look["color"], marker=look["marker"], linestyle=look["linestyle"], linewidth=2,
                   markersize=6, label=look["label"] if backend == "cpu" else "%s: %s" % (look["label"], devices[backend]))
    right.set_xscale("log")
    right.set_yscale("log")
    right.set_xlabel("Simulated paths N", color=INK)
    right.set_ylabel("Median time per estimate (s, log scale)", color=INK)
    right.set_title("Run time; the gap between the lines is the GPU speedup", color=INK, fontsize=10, loc="left")
    right.legend(frameon=False, fontsize=8)
    style_axes(right)
    save(figure, results, "risk_convergence.png")

    if "cpu" in medians and "cuda" in medians:
        print("GPU speedup over one CPU thread (median time per estimate):")
        for paths in sorted(set(medians["cpu"]) & set(medians["cuda"])):
            print("  N = %-11d cpu %10.5f s   gpu %10.5f s   speedup %8.1fx"
                  % (paths, medians["cpu"][paths], medians["cuda"][paths], medians["cpu"][paths] / medians["cuda"][paths]))


def plot_risk_live(results):
    path = os.path.join(results, "risk_live.csv")
    if not os.path.isfile(path):
        print("skipping live risk figure: no " + path)
        return
    rows = read_rows(path)
    ticks = [int(r["tick"]) for r in rows]

    figure, (left, right) = plt.subplots(1, 2, figsize=(11.0, 4.0), sharex=True)
    cpu_look, gpu_look = STYLE["cpu"], STYLE["cuda"]

    call = [float(r["call_mc"]) for r in rows]
    band = [2.0 * float(r["call_std_error"]) for r in rows]
    left.plot(ticks, [float(r["call_exact"]) for r in rows], color=MUTED, linewidth=1.5, label="Black-Scholes")
    left.errorbar(ticks, call, yerr=band, color=gpu_look["color"], marker="o", linestyle="none", markersize=5,
                  capsize=3, label="Monte Carlo, 2 standard errors")
    left.set_xlabel("Simulation tick", color=INK)
    left.set_ylabel("Price (ticks)", color=INK)
    left.set_title("At-the-money call on the live market", color=INK, fontsize=10, loc="left")
    left.legend(frameon=False, fontsize=8)
    style_axes(left)

    right.plot(ticks, [float(r["var_exact"]) for r in rows], color=MUTED, linewidth=1.5,
               label="99% VaR, closed form")
    right.plot(ticks, [float(r["var_mc"]) for r in rows], color=gpu_look["color"], marker="o", linestyle="none",
               markersize=5, label="99% VaR, simulated")
    right.plot(ticks, [float(r["es_exact"]) for r in rows], color=MUTED, linewidth=1.5, linestyle="--",
               label="99% expected shortfall, closed form")
    right.plot(ticks, [float(r["es_mc"]) for r in rows], color=cpu_look["color"], marker="s", linestyle="none",
               markersize=5, label="99% expected shortfall, simulated")
    right.set_xlabel("Simulation tick", color=INK)
    right.set_ylabel("Loss per share held (ticks)", color=INK)
    right.set_title("Tail risk of one share over the next 1,000 ticks", color=INK, fontsize=10, loc="left")
    right.legend(frameon=False, fontsize=8)
    style_axes(right)
    save(figure, results, "risk_live.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", default="results", help="directory with the CSV files; figures go here too")
    args = parser.parse_args()

    plot_latency(args.results)
    plot_returns(args.results)
    plot_acf(args.results)
    plot_price(args.results)
    plot_convergence(args.results)
    plot_risk_live(args.results)


if __name__ == "__main__":
    main()
