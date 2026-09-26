set -e

REPO=/home/zyz/microperf-benchmark/repo-clean-1/pitstop
RESULTS=${RESULTS:-$REPO/mixed-compare-results}
mkdir -p "$RESULTS"

VARIANTS=${VARIANTS:-"pitstop pitstop-c2 pitstop-c1 pitstop-n pitstop-e pitstop-h"}

RUNS=${RUNS:-1}
DURATION=${DURATION:-60}
CONCURRENCY=${CONCURRENCY:-10}
WARMUP=${WARMUP:-15}
COUNT=${COUNT:-500}
SCENARIO=mixed
VERIFY=${VERIFY:-1}

LOG="$RESULTS/all-mixed.log"
echo "Mixed-workload comparison started at $(date -Iseconds)" > "$LOG"
echo "Variants: $VARIANTS" | tee -a "$LOG"
echo "Runs=$RUNS Duration=${DURATION}s Concurrency=$CONCURRENCY Warmup=${WARMUP}s Seed=$COUNT Scenario=$SCENARIO Verify=$VERIFY" | tee -a "$LOG"

for variant in $VARIANTS; do
    echo "============================================" | tee -a "$LOG"
    echo "Benchmarking variant: $variant" | tee -a "$LOG"
    echo "============================================" | tee -a "$LOG"
    cd "$REPO/$variant/src/perf-tests"
    env DURATION="$DURATION" CONCURRENCY="$CONCURRENCY" WARMUP="$WARMUP" COUNT="$COUNT" SCENARIO="$SCENARIO" VERIFY="$VERIFY" \
        bash run-benchmark.sh "$RUNS" >> "$LOG" 2>&1 || echo "  WARNING: benchmark for $variant failed" | tee -a "$LOG"
    mkdir -p "$RESULTS/$variant"
    cp reports/*.json "$RESULTS/$variant/" 2>/dev/null || true
    echo "Variant $variant done at $(date -Iseconds)" | tee -a "$LOG"
done

echo "All variants finished at $(date -Iseconds)" | tee -a "$LOG"
echo "Results in $RESULTS" | tee -a "$LOG"
