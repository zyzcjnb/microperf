#!/usr/bin/env bash
# MicroPerf benchmark pipeline (open-source release).
# Usage: benchmark.sh <repo_root> <run_dir>
#   repo_root: directory containing strong-base/ and instances/
#   run_dir:   where results.tsv / logs / meta.log are written
# Every variant is built, deployed, warmed up (30s), and load-tested.
# Latency is extracted from wrk2's "#[Mean = ...]" line (mean, not stddev).
set -u

REPO_ROOT="$1"
RUN_DIR="$2"
mkdir -p "$RUN_DIR/logs"
RESULTS="$RUN_DIR/results.tsv"
[ -f "$RESULTS" ] || echo -e "group\tsystem\tvariant\titer\tavg_latency_ms\trps\terrors\tstatus\tlog" > "$RESULTS"
META="$RUN_DIR/meta.log"
STARTUP_WAIT=30
ITERATIONS=3

log() { echo "[$(date '+%F %T')] $*" | tee -a "$META"; }

record() { # group system variant iter lat rps err status log
  echo -e "$1\t$2\t$3\t$4\t$5\t$6\t$7\t$8\t$9" >> "$RESULTS"
}

stack_down() { # dir composefile
  (cd "$1" && docker compose -f "$2" down -v --remove-orphans) >/dev/null 2>&1
}

parse_wrk() { # stdout -> "lat rps err"
  local out="$1" lat rps err
  lat=$(echo "$out" | grep '#\[Mean' | tail -1 | sed 's/.*Mean = *//; s/[^0-9.].*//')
  rps=$(echo "$out" | grep 'Requests/sec:' | tail -1 | awk '{print $2}')
  err=$(echo "$out" | grep 'Non-2xx or 3xx responses:' | tail -1 | awk '{print $5}')
  echo "${lat:-NA} ${rps:-NA} ${err:-NA}"
}

# ---------- hotel ----------
run_hotel() { # dir iter log -> echoes "lat rps err status"
  local dir="$1" iter="$2" tlog="$3"
  local cdir="$dir/hotelReservation"
  (cd "$cdir" && docker compose build) >> "$tlog" 2>&1 || { echo "NA NA NA BUILD_ERROR"; return; }
  stack_down "$cdir" docker-compose.yml
  (cd "$cdir" && docker compose up -d) >> "$tlog" 2>&1 || { stack_down "$cdir" docker-compose.yml; echo "NA NA NA UP_ERROR"; return; }
  sleep $STARTUP_WAIT
  local out
  out=$(cd "$dir" && timeout 200 ./wrk2/wrk -t 4 -c 100 -d 60s -L \
        -s "$cdir/wrk2/scripts/hotel-reservation/mixed-workload_type_1.lua" \
        -R 5000 http://127.0.0.1:5000 2>&1)
  echo "$out" >> "$tlog"
  stack_down "$cdir" docker-compose.yml
  local parsed; parsed=$(parse_wrk "$out")
  [ "${parsed%% *}" = "NA" ] && parsed="$parsed TEST_ERROR" || parsed="$parsed OK"
  echo "$parsed"
}

# ---------- media ----------
run_media() { # dir iter log
  local dir="$1" iter="$2" tlog="$3"
  local cdir="$dir/mediaMicroservices"
  local tag
  tag=$(grep -oE 'image: media-microservices:[^ ]+' "$cdir/docker-compose.yml" | head -1 | awk '{print $2}')
  if [ -n "$tag" ]; then
    docker build -t "$tag" "$cdir" >> "$tlog" 2>&1 || { echo "NA NA NA BUILD_ERROR"; return; }
  fi
  stack_down "$cdir" docker-compose.yml
  (cd "$cdir" && docker compose up -d) >> "$tlog" 2>&1 || { stack_down "$cdir" docker-compose.yml; echo "NA NA NA UP_ERROR"; return; }
  sleep $STARTUP_WAIT
  (cd "$cdir" && bash scripts/register_users.sh && bash scripts/register_movies.sh) >> "$tlog" 2>&1
  python3 "$(dirname "$0")/register_movies_for_compose.py" "$cdir/wrk2/scripts/media-microservices/compose-review.lua" >> "$tlog" 2>&1 || true
  local resp
  for r in 1 2 3; do
    resp=$(curl -s -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8080/wrk2-api/review/compose \
      -d "username=username_1&password=password_1&title=Avengers%3A%20Endgame&rating=5&text=warmup" 2>/dev/null)
    [ "$resp" = "200" ] && break; sleep 5
  done
  [ "$resp" != "200" ] && echo "WARN sample request=$resp" >> "$tlog"
  local out
  out=$(cd "$dir" && timeout 200 ./wrk2/wrk -t 4 -c 200 -d 60s -L \
        -s "$cdir/wrk2/scripts/media-microservices/compose-review.lua" \
        -R 5000 http://127.0.0.1:8080 2>&1)
  echo "$out" >> "$tlog"
  stack_down "$cdir" docker-compose.yml
  local parsed; parsed=$(parse_wrk "$out")
  [ "${parsed%% *}" = "NA" ] && parsed="$parsed TEST_ERROR" || parsed="$parsed OK"
  echo "$parsed"
}

# ---------- martian-bank-demo ----------
run_martian() { # dir iter log
  local dir="$1" iter="$2" tlog="$3"
  (cd "$dir" && docker compose build) >> "$tlog" 2>&1 || { echo "NA NA NA BUILD_ERROR"; return; }
  stack_down "$dir" docker-compose.yaml
  (cd "$dir" && docker compose up -d) >> "$tlog" 2>&1 || { stack_down "$dir" docker-compose.yaml; echo "NA NA NA UP_ERROR"; return; }
  sleep $STARTUP_WAIT
  (cd "$dir" && docker compose exec -T locust python seed_data.py) >> "$tlog" 2>&1 \
    || { stack_down "$dir" docker-compose.yaml; echo "NA NA NA SEED_ERROR"; return; }
  (cd "$dir" && timeout 400 docker compose exec -T locust /bin/sh ./locust.sh \
      -d 60s -u 60 -r 5 -m direct -o report) >> "$tlog" 2>&1
  local cid; cid=$(cd "$dir" && docker compose ps -q locust 2>/dev/null)
  [ -n "$cid" ] && docker cp "$cid:/service/report" "$tlog.report" >> "$tlog" 2>&1
  stack_down "$dir" docker-compose.yaml
  local lat rps err
  lat=$(grep 'Average Latency (ms):' "$tlog.report" 2>/dev/null | awk '{print $4}')
  rps=$(grep 'Throughput (completed req/s):' "$tlog.report" 2>/dev/null | awk '{print $4}')
  err=$(grep 'Total Failures:' "$tlog.report" 2>/dev/null | awk '{print $3}')
  [ -z "$lat" ] && { echo "NA NA NA TEST_ERROR"; return; }
  echo "$lat ${rps:-NA} ${err:-NA} OK"
}

# ---------- robot-shop ----------
run_robot() { # dir iter log
  local dir="$1" iter="$2" tlog="$3"
  local TAG; TAG=$(grep '^TAG=' "$dir/.env" | cut -d= -f2)
  docker image inspect "robotshop/rs-load:$TAG" >/dev/null 2>&1 || \
    docker tag robotshop/rs-load:2.1.0 "robotshop/rs-load:$TAG" >> "$tlog" 2>&1
  (cd "$dir" && env DOCKER_BUILDKIT=0 docker compose build) >> "$tlog" 2>&1 || { echo "NA NA NA BUILD_ERROR"; return; }
  stack_down "$dir" docker-compose.yaml
  (cd "$dir" && docker compose up -d) >> "$tlog" 2>&1 || { stack_down "$dir" docker-compose.yaml; echo "NA NA NA UP_ERROR"; return; }
  sleep $STARTUP_WAIT
  local rdir="$RUN_DIR/logs/robot_results_$$_$iter"
  mkdir -p "$rdir"
  (cd "$dir/load-gen" && timeout 500 ./benchmark-load.sh -n 50 -t 3m \
      -h http://localhost:18080 -o "$rdir" -p rb) >> "$tlog" 2>&1
  stack_down "$dir" docker-compose.yaml
  python3 - "$rdir" << 'PYEOF'
import json, csv, os, sys
d = sys.argv[1]
try:
    sj = os.path.join(d, 'rb_summary.json')
    if os.path.exists(sj):
        j = json.load(open(sj))
        print(j.get('avg_latency_ms', 'NA'), j.get('throughput_rps', 'NA'), j.get('failure_count', 'NA'), 'OK')
    else:
        rows = list(csv.reader(open(os.path.join(d, 'rb_stats.csv'))))
        hdr = rows[0]; agg = [r for r in rows if len(r) > 2 and r[1] == 'Aggregated']
        i = lambda n: hdr.index(n)
        print(round(float(agg[0][i('Average Response Time')]), 3), agg[0][i('Requests/s')], agg[0][i('Failure Count')], 'OK')
except Exception:
    print('NA NA NA TEST_ERROR')
PYEOF
}

# ---------- pitstop ----------
run_pitstop() { # dir iter log
  local dir="$1" iter="$2" tlog="$3"
  local sdir="$dir/src"
  [ -f "$sdir/perf-tests/run-benchmark.sh" ] || { echo "NA NA NA TEST_ERROR"; return; }
  # run-benchmark.sh is self-contained: build, deploy, reset DB, seed, load-test, teardown.
  # RUNS=1 per outer iteration (we run ITERATIONS outer iterations).
  (cd "$sdir/perf-tests" && timeout 2400 env DURATION=60 CONCURRENCY=10 WARMUP=15 COUNT=500 SCENARIO=mixed VERIFY=1 bash run-benchmark.sh 1) >> "$tlog" 2>&1
  python3 - "$sdir/perf-tests/reports/perf-report-run1.json" << 'PYEOF'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
    mean = d.get('overall', {}).get('mean', 'NA')
    print(mean, 'NA', 'NA', 'OK' if mean not in ('NA', 0, '0') else 'TEST_ERROR')
except Exception:
    print('NA NA NA TEST_ERROR')
PYEOF
}

BASE_NAME() { # system -> original (uninjected) variant directory name
  case "$1" in
    hotel) echo hotelreservation ;;
    media) echo mediaservice ;;
    martian-bank-demo) echo martian-bank-demo ;;
    robot-shop) echo robot-shop ;;
    pitstop) echo pitstop ;;
  esac
}

log "pipeline started, repo=$REPO_ROOT, iterations=$ITERATIONS"
for group in strong-base instances; do
  for sysdir in "$REPO_ROOT/$group"/*/; do
    system=$(basename "$sysdir")
    [ -d "$sysdir" ] || continue
    for vdir in "$sysdir"*/; do
      variant=$(basename "$vdir")
      [ -d "$vdir" ] || continue
      base=$(BASE_NAME "$system")
      # in instances/, skip the plain original variant (== strong-base reference);
      # in strong-base/, only test the original variant directory.
      if [ "$group" = "instances" ] && [ "$variant" = "$base" ]; then continue; fi
      if [ "$group" = "strong-base" ] && [ "$variant" != "$base" ]; then continue; fi
      fn="run_$system"; [ "$system" = "robot-shop" ] && fn=run_robot; [ "$system" = "martian-bank-demo" ] && fn=run_martian
      log ">>> $group/$system/$variant"
      for iter in $(seq 1 $ITERATIONS); do
        tlog="$RUN_DIR/logs/${group}__${system}__${variant}__iter${iter}.log"
        res=$( "$fn" "$vdir" "$iter" "$tlog" < /dev/null )
        read -r lat rps err status <<< "$res"
        record "$group" "$system" "$variant" "$iter" "$lat" "$rps" "$err" "$status" "$tlog"
        log "    iter$iter: lat=$lat rps=$rps err=$err $status"
      done
    done
  done
done
log "pipeline finished"
