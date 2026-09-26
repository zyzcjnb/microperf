#!/bin/bash
set -e

LOG="/home/zyz/microperf-benchmark/repo-clean-1/pitstop/benchmark.log"
echo "Starting benchmark at $(date -Iseconds)" > "$LOG"

echo "===== Running baseline pitstop benchmark (2 runs) =====" | tee -a "$LOG"
cd /home/zyz/microperf-benchmark/repo-clean-1/pitstop/pitstop/src/perf-tests
bash run-benchmark.sh 2 >> "$LOG" 2>&1

echo "===== Running pitstop-c2 benchmark (2 runs) =====" | tee -a "$LOG"
cd /home/zyz/microperf-benchmark/repo-clean-1/pitstop/pitstop-c2/src/perf-tests
NO_CACHE=1 bash run-benchmark.sh 2 >> "$LOG" 2>&1

echo "===== Benchmark finished at $(date -Iseconds) =====" | tee -a "$LOG"
