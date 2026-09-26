#!/bin/sh

# set -e

if [ -z "$HOST" ]
then
    echo "HOST env not set"
    exit 1
fi

if ! echo "$NUM_CLIENTS" | egrep -q '^[0-9]+$'
then
    echo "NUM_CLIENTS $NUM_CLIENTS is not a number"
    exit 1
fi

if [ "$NUM_CLIENTS" -eq 0 ]
then
    NUM_CLIENTS=1
fi

if [ "$RUN_TIME" = "0" ]
then
    echo "RUN_TIME must be set for benchmark mode, e.g. 10m or 1h30m"
    exit 1
fi

if ! echo "$RUN_TIME" | egrep -q '^([0-9]+h)?([0-9]+m)?$'
then
    echo "Wrong time format, use 2h42m"
    exit 1
fi

SPAWN_RATE=${SPAWN_RATE:-1}
RESULT_DIR=${RESULT_DIR:-/results}
RESULT_PREFIX=${RESULT_PREFIX:-benchmark}

mkdir -p "$RESULT_DIR"

echo "Running mixed benchmark with $NUM_CLIENTS users for $RUN_TIME"
locust -f robot-shop.py --host "$HOST" --headless -r "$SPAWN_RATE" -u "$NUM_CLIENTS" -t "$RUN_TIME" --csv "$RESULT_DIR/$RESULT_PREFIX"

STATS_FILE="$RESULT_DIR/${RESULT_PREFIX}_stats.csv"
SUMMARY_FILE="$RESULT_DIR/${RESULT_PREFIX}_summary.json"

python3 - "$STATS_FILE" "$SUMMARY_FILE" <<'PY'
import csv
import json
import sys

stats_file = sys.argv[1]
summary_file = sys.argv[2]

def pick(row, *names, default="0"):
    for name in names:
        if name in row and row[name] not in (None, ""):
            return row[name]
    return default

with open(stats_file, newline="", encoding="utf-8") as f:
    rows = list(csv.DictReader(f))

agg = None
for row in rows:
    if row.get("Name") == "Aggregated":
        agg = row
        break

if agg is None:
    raise SystemExit("Unable to find Aggregated row in locust stats output")

request_count = float(pick(agg, "Request Count"))
failure_count = float(pick(agg, "Failure Count"))
avg_latency_ms = float(pick(agg, "Average Response Time"))
throughput_rps = float(pick(agg, "Requests/s"))
error_rate_pct = (failure_count / request_count * 100.0) if request_count > 0 else 0.0

summary = {
    "avg_latency_ms": round(avg_latency_ms, 2),
    "throughput_rps": round(throughput_rps, 2),
    "error_rate_pct": round(error_rate_pct, 4),
    "request_count": int(request_count),
    "failure_count": int(failure_count),
}

endpoint_stats = []
for row in rows:
    if row.get("Name") == "Aggregated":
        continue

    method = (row.get("Type") or "").strip()
    endpoint = (row.get("Name") or "").strip()
    if not method or not endpoint:
        continue

    endpoint_request_count = int(float(pick(row, "Request Count")))
    endpoint_failure_count = int(float(pick(row, "Failure Count")))
    endpoint_completed_request_count = endpoint_request_count - endpoint_failure_count
    endpoint_avg_latency_ms = float(pick(row, "Average Response Time"))

    endpoint_stats.append({
        "method": method,
        "endpoint": endpoint,
        "request_count": endpoint_request_count,
        "completed_request_count": endpoint_completed_request_count,
        "avg_latency_ms": round(endpoint_avg_latency_ms, 2),
    })

endpoint_stats.sort(
    key=lambda item: (-item["request_count"], item["method"], item["endpoint"])
)
summary["endpoint_stats"] = endpoint_stats

with open(summary_file, "w", encoding="utf-8") as out:
    json.dump(summary, out, ensure_ascii=True, indent=2)

print("\\n=== Mixed Benchmark Summary ===")
print(f"Average latency: {summary['avg_latency_ms']} ms")
print(f"Throughput: {summary['throughput_rps']} req/s")
print(f"Error rate: {summary['error_rate_pct']}%")
print(f"Endpoint stats: {len(summary['endpoint_stats'])} entries")
print(f"Saved summary: {summary_file}")
PY