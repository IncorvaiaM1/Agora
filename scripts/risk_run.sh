#!/usr/bin/env bash
# Tier 2 experiments (result 4): Monte Carlo convergence and GPU speedup, then
# the live risk engine watching a mixed market.
#
# Usage: ./scripts/risk_run.sh [--build DIR] [--max-paths N] [--seeds S] [--backend cpu|cuda]
#                              [--ticks T] [--port P] [--skip-convergence]
#
# Run it on a GPU node with a CUDA build (scripts/pace_run.sh --gpu does both).
# Without a GPU it still runs, on the CPU only.
#
# Outputs (commit these): results/convergence.csv, results/risk_live.csv,
# results/risk_agents.csv, results/risk_live.txt.
set -euo pipefail

build=build-cuda
max_paths=1e8
seeds=8
backend=cuda
ticks=20000
port=9500
convergence=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)     build="$2";     shift 2 ;;
        --max-paths) max_paths="$2"; shift 2 ;;
        --seeds)     seeds="$2";     shift 2 ;;
        --backend)   backend="$2";   shift 2 ;;
        --ticks)     ticks="$2";     shift 2 ;;
        --port)      port="$2";      shift 2 ;;
        --skip-convergence) convergence=0; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cd "$(dirname "$0")/.."
mkdir -p results

if [[ "$convergence" == 1 ]]; then
    echo "== convergence and speedup (result 4) =="
    "$build/agora_risk" convergence --min-paths 1e4 --max-paths "$max_paths" --seeds "$seeds" \
        --out results/convergence.csv
fi

# Fall back to the CPU for the live run if this build or node has no GPU.
if [[ "$backend" == cuda ]] && ! grep -q '^cuda,' results/convergence.csv; then
    echo "no GPU results above, so the live risk engine runs on the CPU"
    backend=cpu
fi

echo "== live risk engine on a mixed market ($backend) =="
./scripts/run_market.sh --mix "mm:2,zi:6,value:4,trend:4" --ticks "$ticks" --seed 42 --port "$port" \
    --out logs/risk_live --build "$build" --risk "$backend" --risk-every 1000 | tail -n 2
cp logs/risk_live/risk.csv results/risk_live.csv
cp logs/risk_live/risk_agents.csv results/risk_agents.csv
cp logs/risk_live/risk.txt results/risk_live.txt
tail -n 20 results/risk_live.txt
