#!/usr/bin/env bash
# MicroPerf open-source benchmark driver.
# Usage: ./run-benchmark.sh [run_name]
# Tests every variant under repo/strong-base and repo/instances, 3 iterations each.
# Requires: docker, docker compose, python3, curl.
set -u
cd "$(dirname "$0")"
RUN_NAME="${1:-run-$(date +%Y%m%d-%H%M%S)}"
RUN_DIR="results/$RUN_NAME"
mkdir -p "$RUN_DIR"
echo "Run dir: $RUN_DIR"
bash scripts/benchmark.sh "$PWD/repo" "$RUN_DIR"
echo "Done. Results: $RUN_DIR/results.tsv"
