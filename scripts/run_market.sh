#!/usr/bin/env bash
# Runs one market session: starts the exchange, launches N agent processes,
# waits for the session to end and leaves the trade and command logs behind.
#
# Usage: ./scripts/run_market.sh [--agents N] [--ticks T] [--seed S] [--port P]
#                                [--queue spsc|mutex] [--live] [--tick-ms M]
#                                [--out DIR] [--build DIR]
#
# Lockstep mode is the default, so two runs with the same arguments produce
# byte-identical trade logs. Pass --live for real-time mode.
set -euo pipefail

agents=8
ticks=2000
seed=42
port=9000
queue=spsc
mode=lockstep
tick_ms=1
out=logs
build=build

while [[ $# -gt 0 ]]; do
    case "$1" in
        --agents)  agents="$2";  shift 2 ;;
        --ticks)   ticks="$2";   shift 2 ;;
        --seed)    seed="$2";    shift 2 ;;
        --port)    port="$2";    shift 2 ;;
        --queue)   queue="$2";   shift 2 ;;
        --tick-ms) tick_ms="$2"; shift 2 ;;
        --out)     out="$2";     shift 2 ;;
        --build)   build="$2";   shift 2 ;;
        --live)    mode=live;    shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$out"
exchange_args=(--port "$port" --queue "$queue" --seed "$seed" --agents "$agents" --ticks "$ticks"
               --trades "$out/trades.csv" --commands "$out/commands.bin")
if [[ "$mode" == lockstep ]]; then
    exchange_args+=(--lockstep)
else
    exchange_args+=(--tick-ms "$tick_ms")
fi

"$build/agora_exchange" "${exchange_args[@]}" &
exchange_pid=$!
# If anything below fails, do not leave the exchange running in the background.
trap 'kill "$exchange_pid" 2>/dev/null || true' EXIT

# Wait until the exchange accepts connections (bash's /dev/tcp needs no extra tools).
for _ in $(seq 1 100); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
        break
    fi
    sleep 0.05
done

agent_pids=()
for id in $(seq 1 "$agents"); do
    "$build/agora_agent" --type zi --id "$id" --seed "$seed" --port "$port" &
    agent_pids+=($!)
done

status=0
for pid in "${agent_pids[@]}"; do
    wait "$pid" || status=1
done
wait "$exchange_pid" || status=1
trap - EXIT

echo "trade log: $out/trades.csv ($(($(wc -l < "$out/trades.csv") - 1)) trades)"
exit "$status"
