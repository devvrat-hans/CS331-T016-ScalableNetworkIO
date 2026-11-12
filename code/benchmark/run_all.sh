#!/usr/bin/env bash
# Runs the whole study on a real Linux machine. Safe to re-run: finished runs are kept.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-results/final}"
source ./check_env.sh

ulimit -n 65536
make clean >/dev/null
make
mkdir -p "$OUT"
echo "== Correctness tests =="
python3 -B correctness.py "$OUT/correctness" | tee "$OUT/correctness.log" | tail -1
echo "== Syscall counters against strace =="
python3 -B experiments.py check-counters --out "$OUT/counter-check" | tail -5
echo "== Load-generator cross-check against the independent Python client: about 5 minutes =="
python3 -B experiments.py crosscheck --out "$OUT/crosscheck" | tail -5
echo "== Benchmarks: 1200 runs, about 4.5 hours =="
python3 -B experiments.py run --out "$OUT/data"
python3 -B experiments.py analyze --input "$OUT/data" --out "$OUT/report"
echo "== Latency-spike diagnosis (now including SQPOLL): about 12 minutes =="
python3 -B experiments.py diagnose --out "$OUT/spike-diagnosis" | tail -40
echo "Done. Send the whole $OUT folder to the team. Open $OUT/report/report.md"
