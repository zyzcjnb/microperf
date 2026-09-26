#!/bin/bash
set -e
REPO=/home/zyz/microperf-benchmark/repo-clean-1/pitstop
RESULTS=$REPO/benchmark-results-$(date +%Y%m%d-%H%M%S)
mkdir -p "$RESULTS"

RUNS=2
DURATION=60
CONCURRENCY=30
WARMUP=15
COUNT=500
SCENARIO=mixed

for variant in pitstop pitstop-c2 pitstop-c1 pitstop-n pitstop-e; do
    echo "============================================"
    echo "Benchmarking variant: $variant"
    echo "============================================"
    cd "$REPO/$variant/src/perf-tests"
    LOG="$RESULTS/$variant.log"
    env RUNS="$RUNS" DURATION="$DURATION" CONCURRENCY="$CONCURRENCY" WARMUP="$WARMUP" COUNT="$COUNT" SCENARIO="$SCENARIO" \
        bash run-benchmark.sh "$RUNS" > "$LOG" 2>&1 || true
    mkdir -p "$RESULTS/$variant-reports"
    cp "$REPO/$variant/src/perf-tests/reports"/*.json "$RESULTS/$variant-reports/" 2>/dev/null || true
    echo "Variant $variant complete. Log: $LOG"
done

echo "All benchmarks complete. Results in $RESULTS"
