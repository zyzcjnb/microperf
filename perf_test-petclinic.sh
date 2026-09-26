#!/usr/bin/env bash
# MicroPerf 性能测试脚本（开源版，单系统 10 线程 × 3 轮）
# 用法: bash perf_test.sh <项目目录> [label]
# 依赖: JDK17(自动探测), maven, docker, JMeter 5.6.3
set -uo pipefail

SYS_DIR="${1:?用法: bash perf_test.sh <项目目录> [label]}"
LABEL="${2:-$(basename "$SYS_DIR")}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JMX="$ROOT/test_plan.jmx"
JMETER="${JMETER:-/home/zyz/apache-jmeter-5.6.3/bin/jmeter}"
RUNS=3
THREADS="${THREADS:-10}"

# JDK17 探测
if [ ! -d /home/zyz/jdk-17 ]; then echo "ERROR: 未找到 /home/zyz/jdk-17"; exit 1; fi
export JAVA_HOME=/home/zyz/jdk-17
export PATH="$JAVA_HOME/bin:$PATH"

cd "$SYS_DIR" || { echo "目录不存在"; exit 1; }
COMPOSE=docker-compose.yml

echo "===== [$LABEL] mvn package ====="
mvn -q package -DskipTests -o 2>&1 | tail -3 || mvn -q package -DskipTests 2>&1 | tail -3 || { echo "BUILD FAILED"; exit 1; }

# 解析 compose 的 服务→镜像 映射
declare -A IMG
cur=""
while IFS= read -r line; do
  if [[ "$line" =~ ^\ \ ([a-z0-9-]+):$ ]]; then cur="${BASH_REMATCH[1]}"; fi
  if [[ "$line" =~ ^\ \ +image:\ (.+)$ ]] && [ -n "$cur" ]; then IMG[$cur]="${BASH_REMATCH[1]}"; fi
done < "$COMPOSE"

port_of() {
  case "$1" in
    *config-server*) echo 8888;; *discovery-server*) echo 8761;; *api-gateway*) echo 8080;;
    *customers*|*owners*) echo 8081;; *visits*) echo 8082;; *vets*) echo 8083;;
    *genai*) echo 8084;; *pets-service*) echo 8085;; *pettypes*) echo 8086;; *admin*) echo 9090;; *) echo 8080;;
  esac
}

build_image() {
  local mod="$1" image="$2"
  local jar; jar=$(ls "$mod"/target/*.jar 2>/dev/null | grep -v "original\|sources" | head -1)
  [ -z "$jar" ] && return 1
  local base; base=$(basename "$jar" .jar)
  docker build -q -f docker/Dockerfile --build-arg ARTIFACT_NAME="target/$base" \
    --build-arg EXPOSED_PORT="$(port_of "$mod")" -t "$image" "$mod" >/dev/null
}

echo "===== [$LABEL] 构建镜像 ====="
for mod in spring-petclinic-*-service spring-petclinic-api-gateway; do
  [ -d "$mod" ] || continue
  svc="${mod#spring-petclinic-}"
  [ -n "${IMG[$svc]:-}" ] || continue
  echo "  build ${IMG[$svc]}"
  build_image "$mod" "${IMG[$svc]}" || echo "  WARN: $mod 镜像构建失败(若不被路由可忽略)"
done

# 起栈(排除监控类)
SERVICES=$(grep -oE "^  [a-z0-9-]+:" "$COMPOSE" | tr -d ' :' | grep -vE "^(admin-server|grafana-server|prometheus-server)$")
echo "===== [$LABEL] 起栈: $SERVICES ====="
docker rm -f config-server discovery-server customers-service visits-service vets-service owners-service \
  pets-service pettypes-service genai-service api-gateway tracing-server admin-server hsqldb-server >/dev/null 2>&1 || true
docker compose up -d $SERVICES >/dev/null || { echo "COMPOSE UP FAILED"; exit 1; }

echo "===== [$LABEL] 等待就绪(最长 600s) ====="
ready=0
for i in $(seq 1 120); do
  code=$(curl -s -o /dev/null -w "%{http_code}" -m 5 "http://localhost:8080/api/customer/owners" 2>/dev/null || echo 000)
  [ "$code" = "200" ] && { ready=1; break; }
  sleep 5
done
[ "$ready" = "1" ] || { echo "READY TIMEOUT (last=$code)"; docker compose down -v >/dev/null 2>&1; exit 1; }

# 预热
for i in 1 2 3 4 5; do curl -s -m 3 -o /dev/null "http://localhost:8080/api/customer/owners" & sleep 1; done; wait 2>/dev/null || true

echo "===== [$LABEL] JMeter ${THREADS}线程 × ${RUNS}轮 ====="
AVGS=(); TPSS=(); ERRS=()
for run in $(seq 1 $RUNS); do
  OUT="$ROOT/results/${LABEL}_run${run}"
  mkdir -p "$ROOT/results"
  $JMETER -n -t "$JMX" -Jthreads=$THREADS -JTARGET_HOST=localhost -JTARGET_PORT=8080 \
    -l "${OUT}.jtl" > "${OUT}.out" 2>&1
  line=$(grep "summary =" "${OUT}.out" | tail -1)
  echo "  run$run: $line"
  avg=$(echo "$line" | sed -E 's/.*Avg:\s*([0-9]+).*/\1/')
  tps=$(echo "$line" | sed -E 's/.*=\s*([0-9.]+)\/s.*/\1/')
  err=$(echo "$line" | sed -E 's/.*Err:\s*[0-9]+ \(([0-9.]+)%\).*/\1/')
  AVGS+=("$avg"); TPSS+=("$tps"); ERRS+=("$err")
done

mean() { python3 -c "import sys; v=[float(x) for x in sys.argv[1:]]; print(round(sum(v)/len(v),1))" "$@"; }
M_AVG=$(mean "${AVGS[@]}"); M_TPS=$(mean "${TPSS[@]}"); M_ERR=$(mean "${ERRS[@]}")
echo "===== [$LABEL] 结果: 平均延迟=${M_AVG}ms 吞吐=${M_TPS}/s 错误率=${M_ERR}% ====="

docker compose down -v --remove-orphans >/dev/null 2>&1
echo "$LABEL,$M_AVG,$M_TPS,$M_ERR" >> "$ROOT/results/summary.csv"
exit 0
