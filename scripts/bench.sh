#!/usr/bin/env bash
# Latency and throughput sweep for results 1 and 2.
#
# For each queue kind (spsc, mutex) and each client count, starts a fresh
# exchange, runs agora_bench_latency against it several times, and appends one
# CSV line per run. Every configuration uses the same seeds, so both queues
# see the same order flow.
#
# Usage: ./scripts/bench.sh [--clients "1 4 16 64"] [--orders M] [--window W]
#                           [--repeats R] [--port P] [--out FILE] [--build DIR]
#
# Run it on a quiet machine (a PACE-ICE compute node, not a login node).
set -euo pipefail

clients_list="1 4 16 64"
orders=20000
window=1
repeats=5
port=9200
out=results/latency.csv
build=build

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clients) clients_list="$2"; shift 2 ;;
        --orders)  orders="$2";       shift 2 ;;
        --window)  window="$2";       shift 2 ;;
        --repeats) repeats="$2";      shift 2 ;;
        --port)    port="$2";         shift 2 ;;
        --out)     out="$2";          shift 2 ;;
        --build)   build="$2";        shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$(dirname "$out")"
echo "queue,clients,window,orders,seconds,orders_per_s,mean_us,p50_us,p99_us,p999_us,max_us,repeat" > "$out"

exchange_pid=""
trap '[[ -n "$exchange_pid" ]] && kill "$exchange_pid" 2>/dev/null || true' EXIT

for queue in spsc mutex; do
    for clients in $clients_list; do
        for repeat in $(seq 1 "$repeats"); do
            # A fresh exchange per run, so every run starts from an empty book.
            "$build/agora_exchange" --port "$port" --queue "$queue" --tick-ms 0 > /dev/null &
            exchange_pid=$!
            for _ in $(seq 1 100); do
                if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
                    break
                fi
                sleep 0.05
            done

            line=$("$build/agora_bench_latency" --port "$port" --clients "$clients" --orders "$orders" \
                       --window "$window" --seed "$repeat" --label "$queue" --csv)
            echo "$line,$repeat" >> "$out"
            echo "$line,$repeat"

            kill -INT "$exchange_pid"
            wait "$exchange_pid" || true
            exchange_pid=""
        done
    done
done

echo "wrote $out"
