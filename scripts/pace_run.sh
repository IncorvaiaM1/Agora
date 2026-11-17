#!/usr/bin/env bash
# Everything Tier 1 needs from PACE-ICE, in one go: build, test, the latency
# sweep (results 1 and 2) and the market statistics (result 3).
#
# Run it from inside an interactive session on a compute node, not on a login
# node: the latency numbers are only meaningful on cores you are not sharing.
#
# Usage: ./scripts/pace_run.sh [--quick] [--skip-bench] [--skip-stats]
#
#   --quick       small sizes, about a minute; use it first to check the setup
#   --skip-bench  do not run the latency sweep
#   --skip-stats  do not run the market statistics
#
# Outputs (commit these): results/environment.txt, results/latency.csv,
# results/stylized_*.csv, results/acf_squared.csv, results/returns_hist.csv,
# results/price_path.csv and, if matplotlib is available, the PNG figures.
set -euo pipefail

quick=0
run_bench=1
run_stats=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)      quick=1;     shift ;;
        --skip-bench) run_bench=0; shift ;;
        --skip-stats) run_stats=0; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cd "$(dirname "$0")/.."
mkdir -p results

# Record where the numbers came from; the report and README quote this file.
{
    echo "date:     $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "host:     $(hostname)"
    echo "cpu:      $(lscpu | sed -n 's/^Model name: *//p')"
    echo "cores:    $(nproc) available to this session"
    echo "kernel:   $(uname -sr)"
    echo "compiler: $(c++ --version | head -n 1)"
    echo "cmake:    $(cmake --version | head -n 1)"
    echo "commit:   $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    # `module` is a shell function that scripts do not always inherit.
    if type module > /dev/null 2>&1; then
        echo "modules:  $(module -t list 2>&1 | tr '\n' ' ')"
    else
        echo "modules:  (module command not available in this shell)"
    fi
} > results/environment.txt
cat results/environment.txt

echo "== build (headless: no CUDA, no OpenGL) =="
cmake -S . -B build -DAGORA_WITH_CUDA=OFF -DAGORA_WITH_GL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"

echo "== unit tests =="
ctest --test-dir build --output-on-failure

if [[ "$run_bench" == 1 ]]; then
    echo "== latency sweep (results 1 and 2) =="
    if [[ "$quick" == 1 ]]; then
        ./scripts/bench.sh --clients "1 4" --orders 2000 --repeats 1
    else
        ./scripts/bench.sh
    fi
fi

if [[ "$run_stats" == 1 ]]; then
    echo "== market statistics (result 3) =="
    if [[ "$quick" == 1 ]]; then
        ./scripts/stylized_facts.sh --seeds "1 2" --ticks 5000
    else
        ./scripts/stylized_facts.sh
    fi
fi

echo "== figures =="
if python3 -c "import matplotlib" 2>/dev/null; then
    python3 scripts/plots.py --results results
else
    echo "matplotlib not found in this Python; the CSV files are complete."
    echo "Activate an environment that has it (for example: conda activate gdl) and run"
    echo "  python3 scripts/plots.py"
fi

echo "done. Commit the results/ folder."
