#!/bin/bash
set -e

BASE_URL=${BASE_URL:-http://localhost:7005}
CUSTOMER_API=${CUSTOMER_API:-http://localhost:5100}
VEHICLE_API=${VEHICLE_API:-http://localhost:5001}
WORKSHOP_API=${WORKSHOP_API:-http://localhost:5200}
TIMEOUT=${TIMEOUT:-300}

echo "Waiting for services to be ready (timeout ${TIMEOUT}s)..."

start_time=$(date +%s)

wait_for() {
    local url=$1
    local name=$2
    while true; do
        if curl -sf --max-time 5 "${url}/hc" > /dev/null 2>&1; then
            echo "  ${name} is ready"
            return 0
        fi
        elapsed=$(($(date +%s) - start_time))
        if [ "$elapsed" -ge "$TIMEOUT" ]; then
            echo "  Timeout waiting for ${name}"
            return 1
        fi
        echo "  Waiting for ${name}..."
        sleep 3
    done
}

wait_for "$CUSTOMER_API" "CustomerManagementAPI"
wait_for "$VEHICLE_API" "VehicleManagementAPI"
wait_for "$WORKSHOP_API" "WorkshopManagementAPI"

if [ "${SKIP_WEBAPP:-0}" != "1" ]; then
    wait_for "$BASE_URL" "WebApp"
fi

echo "All services are ready."
