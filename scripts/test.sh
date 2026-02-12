set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/tests/out"
mkdir -p "$OUT"

MANAGER="$ROOT/bin/manager"
WORKER="$ROOT/bin/worker"
HOST="127.0.0.1"
BASE_PORT=7000
STEPS=200000

cleanup() {
  pkill -f "$MANAGER" >/dev/null 2>&1 || true
  pkill -f "$WORKER" >/dev/null 2>&1 || true
}
trap cleanup EXIT

run_manager_workers() {
  local workers="$1"
  local cores="$2"
  local port="$3"
  local prefix="$4"
  "$MANAGER" "$workers" "$HOST" "$port" --a 0 --b 1 --n "$STEPS" --timeout 20 >"$OUT/${prefix}.txt" 2>"$OUT/${prefix}.err" &
  local mpid=$!
  sleep 0.2
  for ((i=1;i<=workers;i++)); do
    "$WORKER" --host "$HOST" --port "$port" --cores "$cores" --timeout 20 >"$OUT/${prefix}_w${i}.txt" 2>"$OUT/${prefix}_w${i}.err" &
  done
  wait "$mpid"
}

echo "[TEST] baseline 1 worker x 1 core"
run_manager_workers 1 1 "$BASE_PORT" run1
VAL1=$(awk -F= '/^INTEGRAL=/{print $2}' "$OUT/run1.txt")
T1=$(awk -F= '/^TOTAL_TIME_SEC=/{print $2}' "$OUT/run1.txt")

echo "[TEST] non-basic 2 workers x 2 cores"
run_manager_workers 2 2 "$((BASE_PORT + 1))" run2
VAL2=$(awk -F= '/^INTEGRAL=/{print $2}' "$OUT/run2.txt")
T2=$(awk -F= '/^TOTAL_TIME_SEC=/{print $2}' "$OUT/run2.txt")

VAL1="$VAL1" VAL2="$VAL2" python3 - <<'PY'
import math, os, sys
v1=float(os.environ["VAL1"])
v2=float(os.environ["VAL2"])
ok = abs(v1-math.pi)<1e-4 and abs(v2-math.pi)<1e-4
print("[ASSERT] correctness:", "OK" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY

T1="$T1" T2="$T2" python3 - <<'PY'
import os
t1=float(os.environ["T1"]); t2=float(os.environ["T2"])
print(f"[CHECK] speedup: t1={t1:.4f}s t2={t2:.4f}s -> {'OK' if t2 < t1 else 'WARN'}")
PY

echo "[TEST] failure detection (no workers)"
set +e
"$MANAGER" 1 "$HOST" "$((BASE_PORT + 2))" --a 0 --b 1 --n "$STEPS" --timeout 2 >"$OUT/fail.txt" 2>"$OUT/fail.err"
RC=$?
set -e
if [[ "$RC" -eq 0 ]]; then
  echo "[ASSERT] expected non-zero manager exit on worker absence"
  exit 1
fi
echo "[ASSERT] failure path: OK"

echo "OK"

