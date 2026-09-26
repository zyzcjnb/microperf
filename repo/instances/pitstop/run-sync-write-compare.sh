#!/bin/bash
set -e

REPO=/home/zyz/microperf-benchmark/repo-clean-1/pitstop
RESULTS=$REPO/sync-write-compare-results
mkdir -p "$RESULTS"

RUNS=${RUNS:-2}
DURATION=${DURATION:-60}
CONCURRENCY=${CONCURRENCY:-10}
WARMUP=${WARMUP:-15}
COUNT=${COUNT:-500}
SCENARIO=${SCENARIO:-write}

export SKIP_WEBAPP=1

LOG="$RESULTS/compare.log"
echo "Starting sync-write comparison at $(date -Iseconds)" > "$LOG"

echo "===== Running baseline pitstop benchmark (${RUNS} runs, ${SCENARIO}) =====" | tee -a "$LOG"
cd "$REPO/pitstop/src/perf-tests"
env RUNS="$RUNS" DURATION="$DURATION" CONCURRENCY="$CONCURRENCY" WARMUP="$WARMUP" COUNT="$COUNT" SCENARIO="$SCENARIO" VERIFY="${VERIFY:-1}" \
    bash run-benchmark.sh "$RUNS" >> "$LOG" 2>&1
mkdir -p "$RESULTS/pitstop-reports"
cp reports/*.json "$RESULTS/pitstop-reports/" 2>/dev/null || true

echo "===== Running pitstop-e benchmark (${RUNS} runs, ${SCENARIO}) =====" | tee -a "$LOG"
cd "$REPO/pitstop-e/src/perf-tests"
env RUNS="$RUNS" DURATION="$DURATION" CONCURRENCY="$CONCURRENCY" WARMUP="$WARMUP" COUNT="$COUNT" SCENARIO="$SCENARIO" VERIFY="${VERIFY:-1}" \
    bash run-benchmark.sh "$RUNS" >> "$LOG" 2>&1
mkdir -p "$RESULTS/pitstop-e-reports"
cp reports/*.json "$RESULTS/pitstop-e-reports/" 2>/dev/null || true

echo "===== Comparison finished at $(date -Iseconds) =====" | tee -a "$LOG"
echo "Results in $RESULTS" | tee -a "$LOG"
