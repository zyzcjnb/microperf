# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

#!/bin/sh

set -eu

#######################################################################################
## Config

# Parameter defaults (can be overridden by CLI args)
RUN_TIME="60s"
USERS="20"
SPAWN_RATE="5"
TARGET_MODE="direct"

usage() {
	echo "Usage: $0 [-d DURATION] [-u USERS] [-r SPAWN_RATE] [-m MODE] [-o REPORT_FILE]"
	echo ""
	echo "Options:"
	echo "  -d DURATION    Test duration, e.g. 60s, 2m (default: 60s)"
	echo "  -u USERS       Total concurrent users (default: 20)"
	echo "  -r SPAWN_RATE  Users spawned per second (default: 5)"
	echo "  -m MODE        Target mode: direct|gateway (default: direct)"
	echo "  -o REPORT_FILE Output report file path/name"
	echo "  -h             Show this help"
}

while getopts "d:u:r:o:m:h" opt; do
	case "$opt" in
		d) RUN_TIME="$OPTARG" ;;
		u) USERS="$OPTARG" ;;
		r) SPAWN_RATE="$OPTARG" ;;
		o) REPORT_FILE="$OPTARG" ;;
		m) TARGET_MODE="$OPTARG" ;;
		h)
			usage
			exit 0
			;;
		*)
			usage
			exit 1
			;;
	esac
done

REPORT_FILE="${REPORT_FILE:-mixed_test_result_$(date +%Y%m%d_%H%M%S).txt}"
WORK_DIR="${WORK_DIR:-.}"
STATS_DIR="$WORK_DIR/.locust_stats_tmp"
TARGET_CONFIG_FILE="${TARGET_CONFIG_FILE:-$WORK_DIR/target_profiles.env}"

if [ ! -f "$TARGET_CONFIG_FILE" ]; then
	echo "Target config file not found: $TARGET_CONFIG_FILE"
	exit 1
fi

# shellcheck disable=SC1090
. "$TARGET_CONFIG_FILE"

apply_target_mode() {
	mode="$1"
	case "$mode" in
		direct)
			export VITE_USERS_URL="$DIRECT_VITE_USERS_URL"
			export VITE_ATM_URL="$DIRECT_VITE_ATM_URL"
			export VITE_ACCOUNTS_URL="$DIRECT_VITE_ACCOUNTS_URL"
			export VITE_TRANSFER_URL="$DIRECT_VITE_TRANSFER_URL"
			export VITE_LOAN_URL="$DIRECT_VITE_LOAN_URL"
			;;
		gateway)
			export VITE_USERS_URL="$GATEWAY_VITE_USERS_URL"
			export VITE_ATM_URL="$GATEWAY_VITE_ATM_URL"
			export VITE_ACCOUNTS_URL="$GATEWAY_VITE_ACCOUNTS_URL"
			export VITE_TRANSFER_URL="$GATEWAY_VITE_TRANSFER_URL"
			export VITE_LOAN_URL="$GATEWAY_VITE_LOAN_URL"
			;;
		*)
			echo "Unsupported mode: $mode (use direct|gateway)"
			exit 1
			;;
	esac
}

apply_target_mode "$TARGET_MODE"

rm -rf "$STATS_DIR"
mkdir -p "$STATS_DIR"

# Convert duration to seconds for throughput calculation.
# Supports simple forms: Ns, Nm, Nh; falls back to 60 if unknown.
duration_to_seconds() {
	raw="$1"
	case "$raw" in
		*s) echo "${raw%s}" ;;
		*m)
			mins="${raw%m}"
			echo $((mins * 60))
			;;
		*h)
			hours="${raw%h}"
			echo $((hours * 3600))
			;;
		*) echo "60" ;;
	esac
}

TOTAL_DURATION_SECONDS="$(duration_to_seconds "$RUN_TIME")"

#######################################################################################
## Run mixed parallel scenario and export CSV only (concise final report afterwards)

locust -f mixed_parallel_locust.py \
	--headless \
	--only-summary \
	--loglevel WARNING \
	-u "$USERS" -r "$SPAWN_RATE" --run-time "$RUN_TIME" \
	--csv "$STATS_DIR/mixed_parallel" \
	>/dev/null 2>&1 || true

#######################################################################################
## Build concise report

python3 - "$STATS_DIR" "$TOTAL_DURATION_SECONDS" "$REPORT_FILE" "$TARGET_MODE" <<'PY'
import csv
import glob
import os
import sys

stats_dir = sys.argv[1]
total_duration = float(sys.argv[2]) if float(sys.argv[2]) > 0 else 1.0
report_file = sys.argv[3]
target_mode = sys.argv[4]

files = sorted(glob.glob(os.path.join(stats_dir, "*_stats.csv")))

if not files:
	with open(report_file, "w", encoding="utf-8") as f:
		f.write("Mixed Request Test Report (Concise)\n")
		f.write("No stats files generated.\n")
	print(f"Report written to {report_file}")
	sys.exit(0)

def as_float(value, default=0.0):
	try:
		return float(value)
	except Exception:
		return default

def as_int(value, default=0):
	try:
		return int(float(value))
	except Exception:
		return default

total_requests = 0
total_completed = 0
total_failures = 0
weighted_avg_sum = 0.0

endpoint = {}

for file in files:
	with open(file, newline="", encoding="utf-8") as f:
		reader = csv.DictReader(f)
		for row in reader:
			name = (row.get("Name") or "").strip()
			if not name or name == "Aggregated":
				continue

			req_count = as_int(row.get("Request Count"))
			fail_count = as_int(row.get("Failure Count"))
			completed = max(req_count - fail_count, 0)

			avg = as_float(row.get("Average Response Time"))
			p50 = as_float(row.get("50%", row.get("Median Response Time", 0)))
			p95 = as_float(row.get("95%", 0))
			p99 = as_float(row.get("99%", 0))

			total_requests += req_count
			total_completed += completed
			total_failures += fail_count
			weighted_avg_sum += avg * req_count

			agg = endpoint.setdefault(
				name,
				{
					"requests": 0,
					"completed": 0,
					"failures": 0,
					"avg_weighted_sum": 0.0,
					"p50_weighted_sum": 0.0,
					"p95_weighted_sum": 0.0,
					"p99_weighted_sum": 0.0,
				},
			)

			agg["requests"] += req_count
			agg["completed"] += completed
			agg["failures"] += fail_count
			agg["avg_weighted_sum"] += avg * req_count
			agg["p50_weighted_sum"] += p50 * req_count
			agg["p95_weighted_sum"] += p95 * req_count
			agg["p99_weighted_sum"] += p99 * req_count

overall_avg = (weighted_avg_sum / total_requests) if total_requests else 0.0
overall_throughput = total_completed / total_duration
overall_error_rate = (total_failures / total_requests * 100.0) if total_requests else 0.0

lines = []
lines.append("Mixed Request Test Report (Concise)")
lines.append("=" * 80)
lines.append(f"Target Mode: {target_mode}")
lines.append(f"Total Requests: {total_requests}")
lines.append(f"Total Failures: {total_failures}")
lines.append(f"Overall Error Rate (%): {overall_error_rate:.2f}")
lines.append(f"Average Latency (ms): {overall_avg:.2f}")
lines.append(f"Throughput (completed req/s): {overall_throughput:.2f}")
lines.append("")
lines.append("Per Endpoint Metrics")
lines.append("-" * 80)
lines.append("Endpoint | Completed | Failures | ErrRate(%) | Avg(ms) | P50(ms) | P95(ms) | P99(ms)")
lines.append("-" * 80)

for name in sorted(endpoint.keys()):
	item = endpoint[name]
	reqs = item["requests"]
	if reqs <= 0:
		continue

	avg = item["avg_weighted_sum"] / reqs
	p50 = item["p50_weighted_sum"] / reqs
	p95 = item["p95_weighted_sum"] / reqs
	p99 = item["p99_weighted_sum"] / reqs
	err_rate = (item["failures"] / reqs * 100.0) if reqs else 0.0

	lines.append(
		f"{name} | {item['completed']} | {item['failures']} | {err_rate:.2f} | {avg:.2f} | {p50:.2f} | {p95:.2f} | {p99:.2f}"
	)

content = "\n".join(lines) + "\n"
with open(report_file, "w", encoding="utf-8") as f:
	f.write(content)

print(content, end="")
print(f"Report written to {report_file}")
PY

#######################################################################################
## Cleanup

rm -rf "$STATS_DIR" "__pycache__"

