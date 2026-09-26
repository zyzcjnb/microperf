#!/bin/bash
set -e

LOG="/home/zyz/microperf-benchmark/repo-clean-1/pitstop/c1-benchmark.log"
echo "Starting c1 benchmark at $(date -Iseconds)" > "$LOG"

echo "===== Running baseline pitstop benchmark (2 runs, c1 scenario) =====" | tee -a "$LOG"
cd /home/zyz/microperf-benchmark/repo-clean-1/pitstop/pitstop/src/perf-tests
SCENARIO=c1 bash run-benchmark.sh 2 >> "$LOG" 2>&1

echo "===== Running pitstop-c1 benchmark (2 runs, c1 scenario) =====" | tee -a "$LOG"
cd /home/zyz/microperf-benchmark/repo-clean-1/pitstop/pitstop-c1/src/perf-tests
NO_CACHE=1 SCENARIO=c1 bash run-benchmark.sh 2 >> "$LOG" 2>&1

echo "===== C1 benchmark finished at $(date -Iseconds) =====" | tee -a "$LOG"
