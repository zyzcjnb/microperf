#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Defaults
DURATION=${DURATION:-60}
CONCURRENCY=${CONCURRENCY:-30}
WARMUP=${WARMUP:-15}
COUNT=${COUNT:-500}
OUTPUT=${OUTPUT:-perf-report.json}

echo "=== Pitstop Load Test ==="
echo "Duration: ${DURATION}s, Concurrency: ${CONCURRENCY}, Warmup: ${WARMUP}s, Seed count: ${COUNT}"

# 1. Wait for services
bash wait-for-services.sh

# 2. Seed data
python3 seed.py --count "$COUNT"

# 3. Run load test
python3 loadtest.py \
    --duration "$DURATION" \
    --concurrency "$CONCURRENCY" \
    --warmup "$WARMUP" \
    --output "$OUTPUT"

echo "=== Load test complete ==="
echo "Report: $OUTPUT"
