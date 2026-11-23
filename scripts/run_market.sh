#!/usr/bin/env bash
# Runs one market session: starts the exchange, launches N agent processes,
# waits for the session to end and leaves the trade, command and tick logs behind.
#
# Usage: ./scripts/run_market.sh [--agents N | --mix SPEC] [--ticks T] [--seed S]
#                                [--port P] [--queue spsc|mutex] [--live]
#                                [--tick-ms M] [--out DIR] [--build DIR]
#                                [--risk cpu|cuda] [--risk-every K]
#
# --risk B     Also run the live risk engine (agora_risk live) on backend B as a
#              read-only observer. It writes DIR/risk.csv and DIR/risk_agents.csv.
#
# --agents N   N zero-intelligence agents (the random-walk-like baseline).
# --mix SPEC   A mix of strategies, e.g. --mix "mm:2,zi:6,value:4,trend:4".
#              Agent ids are handed out in the order the mix is written.
#
# Lockstep mode is the default, so two runs with the same arguments produce
# byte-identical trade logs. Pass --live for real-time mode.
set -euo pipefail

agents=8
mix=""
ticks=2000
seed=42
port=9000
queue=spsc
mode=lockstep
tick_ms=1
out=logs
build=build
risk=""
risk_every=1000

while [[ $# -gt 0 ]]; do
    case "$1" in
        --agents)  agents="$2";  shift 2 ;;
        --mix)     mix="$2";     shift 2 ;;
        --ticks)   ticks="$2";   shift 2 ;;
        --seed)    seed="$2";    shift 2 ;;
        --port)    port="$2";    shift 2 ;;
        --queue)   queue="$2";   shift 2 ;;
        --tick-ms) tick_ms="$2"; shift 2 ;;
        --out)     out="$2";     shift 2 ;;
        --build)   build="$2";   shift 2 ;;
        --live)    mode=live;    shift ;;
        --risk)    risk="$2";    shift 2 ;;
        --risk-every) risk_every="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# Expand the mix into one strategy name per agent.
types=()
if [[ -z "$mix" ]]; then
    mix="zi:$agents"
fi
IFS=',' read -ra parts <<< "$mix"
for part in "${parts[@]}"; do
    name="${part%%:*}"
    count="${part##*:}"
    for _ in $(seq 1 "$count"); do
        types+=("$name")
    done
done
agents="${#types[@]}"

mkdir -p "$out"
exchange_args=(--port "$port" --queue "$queue" --seed "$seed" --agents "$agents" --ticks "$ticks"
               --trades "$out/trades.csv" --commands "$out/commands.bin" --tick-log "$out/ticks.csv")
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

# The risk engine connects before the agents, so it sees the market from the first tick.
risk_pid=""
if [[ -n "$risk" ]]; then
    "$build/agora_risk" live --backend "$risk" --port "$port" --every "$risk_every" \
        --out "$out/risk.csv" --agents-out "$out/risk_agents.csv" > "$out/risk.txt" &
    risk_pid=$!
    sleep 0.2
fi

agent_pids=()
for id in $(seq 1 "$agents"); do
    "$build/agora_agent" --type "${types[$((id - 1))]}" --id "$id" --seed "$seed" --port "$port" &
    agent_pids+=($!)
done

status=0
for pid in "${agent_pids[@]}"; do
    wait "$pid" || status=1
done
wait "$exchange_pid" || status=1
if [[ -n "$risk_pid" ]]; then
    wait "$risk_pid" || status=1
    echo "risk engine: $out/risk.csv, $out/risk_agents.csv (log in $out/risk.txt)"
fi
trap - EXIT

echo "trade log: $out/trades.csv ($(($(wc -l < "$out/trades.csv") - 1)) trades)"
exit "$status"
