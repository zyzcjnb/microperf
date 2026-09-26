#!/bin/bash
set -e

LOG="/home/zyz/microperf-benchmark/repo-clean-1/pitstop/n-benchmark.log"
echo "Starting n benchmark at $(date -Iseconds)" > "$LOG"

echo "===== Running baseline pitstop benchmark (2 runs, c2 scenario) =====" | tee -a "$LOG"
cd /home/zyz/microperf-benchmark/repo-clean-1/pitstop/pitstop/src/perf-tests
SCENARIO=c2 bash run-benchmark.sh 2 >> "$LOG" 2>&1

echo "===== Running pitstop-n benchmark (2 runs, c2 scenario) =====" | tee -a "$LOG"
cd /home/zyz/microperf-benchmark/repo-clean-1/pitstop/pitstop-n/src/perf-tests
NO_CACHE=1 SCENARIO=c2 bash run-benchmark.sh 2 >> "$LOG" 2>&1

echo "===== N benchmark finished at $(date -Iseconds) =====" | tee -a "$LOG"
