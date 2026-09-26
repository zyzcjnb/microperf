#!/usr/bin/env bash
# MicroPerf benchmark pipeline (open-source release).
# Usage: perf_pipeline.sh <queue.tsv> <run_dir>
# Queue TSV columns: label \t system \t variant \t abs_dir
# Results appended to <run_dir>/results.tsv (one row per iteration).
# NOTE: avg latency is parsed from wrk's '#[Mean = ...' line (true mean).
set -u

BENCH=/home/zyz/microperf
QUEUE="$1"
RUN_DIR="$2"
mkdir -p "$RUN_DIR/logs"
RESULTS="$RUN_DIR/results.tsv"
[ -f "$RESULTS" ] || echo -e "model\tsystem\tvariant\titer\tavg_latency_ms\trps\terrors\tstatus\tlog" > "$RESULTS"
META="$RUN_DIR/meta.log"
STARTUP_WAIT=30
ITERATIONS=3   # <-- open-source release: 3 iterations per variant

log() { echo "[$(date '+%F %T')] $*" | tee -a "$META"; }

record() { # model system variant iter lat rps err status log
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
  python3 "$BENCH/scripts/register_movies_for_compose.py" >> "$tlog" 2>&1
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

# ---------- martian ----------
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
  local res
  res=$(python3 -c "
import json,csv,os
d='$rdir'
sj=os.path.join(d,'rb_summary.json')
try:
  if os.path.exists(sj):
    j=json.load(open(sj))
    print(j.get('avg_latency_ms','NA'), j.get('throughput_rps','NA'), j.get('failure_count','NA'), 'OK')
  else:
    rows=list(csv.reader(open(os.path.join(d,'rb_stats.csv'))))
    hdr=rows[0]; agg=[r for r in rows if len(r)>2 and r[1]=='Aggregated']
    assert agg, 'no aggregated row'
    i=lambda n: hdr.index(n)
    lat=round(float(agg[0][i('Average Response Time')]),3)
    print(lat, agg[0][i('Requests/s')], agg[0][i('Failure Count')], 'OK')
except Exception:
  print('NA NA NA TEST_ERROR')
")
  echo "$res"
}

# ---------- pitstop ----------
run_pitstop() { # dir iter log
  local dir="$1" iter="$2" tlog="$3"
  local sdir="$dir/src"
  [ -f "$sdir/perf-tests/run-benchmark.sh" ] || { echo "NA NA NA TEST_ERROR"; return; }
  # run-benchmark.sh is self-contained; RUNS=1, 3 iterations performed by outer loop
  (cd "$sdir/perf-tests" && timeout 2400 env DURATION=60 CONCURRENCY=10 WARMUP=15 COUNT=500 SCENARIO=mixed VERIFY=1 bash run-benchmark.sh 1) >> "$tlog" 2>&1
  local res
  res=$(python3 -c "
import json
try:
  d=json.load(open('$sdir/perf-tests/reports/perf-report-run1.json'))
  o=d.get('overall',{})
  mean=o.get('mean','NA')
  print(mean, 'NA', 'NA', 'OK' if mean not in ('NA',0,'0') else 'TEST_ERROR')
except Exception:
  print('NA NA NA TEST_ERROR')
")
  echo "$res"
}

log "pipeline started, queue=$QUEUE, ITERATIONS=$ITERATIONS"
while IFS=$'\t' read -r -u 3 model system variant dir; do
  [[ "$model" =~ ^#.*$ || -z "$model" ]] && continue
  [ ! -d "$dir" ] && { log "SKIP $model/$system/$variant: dir missing"; continue; }
  log ">>> $model/$system/$variant"
  for iter in $(seq 1 $ITERATIONS); do
    tlog="$RUN_DIR/logs/${model}__${system}__${variant}__iter${iter}.log"
    fn="run_$system"; [ "$system" = "robot-shop" ] && fn=run_robot; [ "$system" = "martian-bank-demo" ] && fn=run_martian
    res=$( "$fn" "$dir" "$iter" "$tlog" < /dev/null )
    read -r lat rps err status <<< "$res"
    record "$model" "$system" "$variant" "$iter" "$lat" "$rps" "$err" "$status" "$tlog"
    log "    iter$iter: lat=$lat rps=$rps err=$err $status"
  done
done 3< "$QUEUE"
log "pipeline finished"
