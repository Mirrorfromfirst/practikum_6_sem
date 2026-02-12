set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/tests/bench"
mkdir -p "$OUT"

MANAGER="$ROOT/bin/manager"
WORKER="$ROOT/bin/worker"
HOST="127.0.0.1"
BASE_PORT=7100
CSV="$OUT/results.csv"
N=1000000
TIMEOUT=30

echo "workers,cores,total_cores,time_sec,integral" > "$CSV"

run_case() {
  local workers="$1"
  local cores="$2"
  local port="$3"
  local log="$OUT/w${workers}_c${cores}.txt"
  "$MANAGER" "$workers" "$HOST" "$port" --a 0 --b 1 --n "$N" --timeout "$TIMEOUT" >"$log" 2>"$OUT/w${workers}_c${cores}.err" &
  local mpid=$!
  sleep 0.2
  for ((i=1;i<=workers;i++)); do
    "$WORKER" --host "$HOST" --port "$port" --cores "$cores" --timeout "$TIMEOUT" >/dev/null 2>&1 &
  done
  wait "$mpid" || true

  local t
  local val
  t=$(awk -F= '/^TOTAL_TIME_SEC=/{print $2; f=1} END{if(!f)print "NA"}' "$log")
  val=$(awk -F= '/^INTEGRAL=/{print $2; f=1} END{if(!f)print "NA"}' "$log")
  echo "$workers,$cores,$((workers*cores)),$t,$val" >> "$CSV"
}

p="$BASE_PORT"
for w in 1 2 4; do
  for c in 1 2; do
    echo "[BENCH] workers=$w cores=$c"
    run_case "$w" "$c" "$p"
    p=$((p+1))
  done
done

column -t -s, "$CSV" || cat "$CSV"

