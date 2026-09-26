#!/usr/bin/env bash
# Performance test for the microperf open-source repo: 3 runs per target.
# Usage: perf_test.sh [variant ...]   (default: all dirs in repo root)
set -uo pipefail
REPO="$(cd "$(dirname "$0")" && pwd)"
RESULTS=$REPO/results
mkdir -p "$RESULTS"
# 需要为本机用户配置 docker 的免密 sudo（/etc/sudoers 中 NOPASSWD: /usr/bin/docker）
SUDO() { sudo -n "$@"; }

targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=($(ls -d "$REPO"/*/ | xargs -n1 basename | grep -v results))

for name in "${targets[@]}"; do
  dir="$REPO/$name"
  compose="$dir/socialNetwork"
  [ -d "$compose" ] || { echo "skip $name (no socialNetwork)"; continue; }
  img=$(grep -m1 "image: social-network" "$compose/docker-compose.yml" | awk '{print $2}')
  tags="$img"
  grep -q "social-network-m:latest" "$compose/docker-compose.yml" && tags="$img social-network-m:latest"
  echo "== $name (images: $tags)"
  for t in $tags; do
    ( cd "$compose" && SUDO docker build -q -t "$t" . ) >/dev/null || { echo "  BUILD FAIL $t"; continue; }
  done
  for run in 1 2 3; do
    ( cd "$compose" && SUDO docker compose down -v >/dev/null 2>&1 && SUDO docker compose up -d >/dev/null 2>&1 )
    ok=""
    for i in $(seq 1 120); do
      if curl -s -o /dev/null -w "%{http_code}" http://localhost:8080 2>/dev/null | grep -q 200; then ok=1; break; fi
      sleep 1
    done
    [ -z "$ok" ] && { echo "  run$run: nginx not ready"; continue; }
    ( cd "$compose" && python3 scripts/init_social_graph.py --ip 127.0.0.1 --port 8080 --graph socfb-Reed98 --compose >/dev/null 2>&1 )
    sleep 30
    "$dir/wrk2/wrk" -t12 -c300 -d60s -R5000 -L -E \
      -s "$compose/wrk2/scripts/social-network-determinism/mixed-workload.lua" \
      http://localhost:8080 > "$RESULTS/$name-run$run.txt" 2>&1
    echo "  run$run done"
  done
  ( cd "$compose" && SUDO docker compose down -v >/dev/null 2>&1 )
done
echo "PERF TEST COMPLETE"
