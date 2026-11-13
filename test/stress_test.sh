#!/usr/bin/env bash
# Launches N clients concurrently and takes a `ps` snapshot while they are all
# still connected, as the runtime proof that the server is handling >= N clients
# at once via separate forked processes.
set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-9000}"
N="${3:-10}"
HOLD_SECONDS=5

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT="$SCRIPT_DIR/../bin/client"

echo "launching $N concurrent clients against $HOST:$PORT (each holds its connection ${HOLD_SECONDS}s)"

pids=()
for i in $(seq 1 "$N"); do
    "$CLIENT" -h "$HOST" -p "$PORT" hold "$HOLD_SECONDS" &
    pids+=("$!")
done

sleep 2
echo
echo "--- ps snapshot while $N clients should still be connected ---"
ps -eo pid,ppid,stat,cmd | grep -E 'bin/server|PID' | grep -v grep || true
echo "---"
echo "zombie count right now: $(ps -eo stat | grep -c '^Z' || true)"
echo

for pid in "${pids[@]}"; do
    wait "$pid"
done

echo "all $N clients finished"
