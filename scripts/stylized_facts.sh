#!/usr/bin/env bash
# Result 3 from start to finish: runs the mixed market and the zero-intelligence
# market for several seeds in lockstep mode, then computes the statistics.
#
# Usage: ./scripts/stylized_facts.sh [--seeds "1 2 3 4 5"] [--ticks T] [--mix SPEC]
#                                    [--zi N] [--bar B] [--port P] [--build DIR]
#
# Raw logs go to logs/result3/ (not committed); the statistics go to results/.
# Lockstep runs are deterministic, so the same arguments reproduce the same
# numbers on any machine.
set -euo pipefail

seeds="1 2 3 4 5"
ticks=50000
mix="mm:2,zi:6,value:4,trend:4"
zi=16
bar=10
port=9300
build=build

while [[ $# -gt 0 ]]; do
    case "$1" in
        --seeds) seeds="$2"; shift 2 ;;
        --ticks) ticks="$2"; shift 2 ;;
        --mix)   mix="$2";   shift 2 ;;
        --zi)    zi="$2";    shift 2 ;;
        --bar)   bar="$2";   shift 2 ;;
        --port)  port="$2";  shift 2 ;;
        --build) build="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

here="$(cd "$(dirname "$0")" && pwd)"
raw=logs/result3

for seed in $seeds; do
    echo "seed $seed: mixed market ($mix)"
    "$here/run_market.sh" --mix "$mix" --ticks "$ticks" --seed "$seed" --port "$port" \
        --out "$raw/mixed/seed_$seed" --build "$build" | tail -n 1
    echo "seed $seed: zero-intelligence market ($zi agents)"
    "$here/run_market.sh" --agents "$zi" --ticks "$ticks" --seed "$seed" --port "$port" \
        --out "$raw/zero_intelligence/seed_$seed" --build "$build" | tail -n 1
done

python3 "$here/stats.py" --root "$raw" --out results --bar "$bar"
