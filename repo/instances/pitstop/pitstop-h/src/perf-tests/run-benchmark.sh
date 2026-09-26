#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SRC_DIR"

RUNS=${1:-2}
DURATION=${DURATION:-60}
CONCURRENCY=${CONCURRENCY:-30}
WARMUP=${WARMUP:-15}
COUNT=${COUNT:-500}
SCENARIO=${SCENARIO:-c1}

REPORT_DIR="$SCRIPT_DIR/reports"
mkdir -p "$REPORT_DIR"

echo "========================================"
echo "Pitstop Performance Benchmark"
echo "Source: $SRC_DIR"
echo "Runs: $RUNS | Duration: ${DURATION}s | Concurrency: $CONCURRENCY | Warmup: ${WARMUP}s | Seed: $COUNT | Scenario: $SCENARIO"
echo "========================================"

# Build Docker images. Use NO_CACHE=1 to force a clean rebuild.
echo "Building Docker images..."
cd "$SRC_DIR/scripts"
if [ "${NO_CACHE:-0}" = "1" ]; then
    bash RebuildAllDockerImages.sh --no-cache
else
    bash RebuildAllDockerImages.sh
fi

cd "$SRC_DIR"

# Clean up and pre-create data directories with permissive permissions so
# container users (mssql, rabbitmq) can write to the bind mounts.
# Use a Docker container running as root to remove the entire .containerdata
# directory, which may be owned by root after previous container runs.
echo "Cleaning up previous container data..."
docker run --rm -v "$SRC_DIR:/host" alpine:3.20 \
    sh -c 'rm -rf /host/.containerdata' >/dev/null 2>&1 || true

mkdir -p "$SRC_DIR/.containerdata/sqlserver/mssql" \
         "$SRC_DIR/.containerdata/sqlserver/data" \
         "$SRC_DIR/.containerdata/sqlserver/log" \
         "$SRC_DIR/.containerdata/sqlserver/secrets" \
         "$SRC_DIR/.containerdata/sqlserver/backups" \
         "$SRC_DIR/.containerdata/rabbitmq/var/lib/rabbitmq"
chmod -R 777 "$SRC_DIR/.containerdata/sqlserver" "$SRC_DIR/.containerdata/rabbitmq"

for run in $(seq 1 "$RUNS"); do
    echo ""
    echo "=== Run $run / $RUNS ==="

    # Ensure a clean stack state (bind-mounted data is preserved and reset below)
    docker compose -f docker-compose.yml -f docker-compose.local.yml down 2>/dev/null || true

    # Start SQL Server and RabbitMQ first so we can wait for SQL recovery
    # before starting the API services. This avoids API DB-initialization
    # retries timing out while SQL Server is still recovering system DBs.
    echo "Starting infrastructure services..."
    docker compose -f docker-compose.yml -f docker-compose.local.yml up -d sqlserver rabbitmq

    echo "Waiting for SQL Server to finish recovery..."
    sql_ready=0
    for i in $(seq 1 90); do
        if docker exec sqlserver /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P '8jkGh47hnDw89H@q8LN2' -C -Q "SELECT 1" >/dev/null 2>&1; then
            # Ensure all system databases are online before proceeding.
            db_state_count=$(docker exec sqlserver /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P '8jkGh47hnDw89H@q8LN2' -C -h -1 -W -Q "SET NOCOUNT ON; SELECT COUNT(*) FROM sys.databases WHERE state <> 0" 2>/dev/null | tail -1 | tr -d '[:space:]')
            if [ "$db_state_count" = "0" ]; then
                sql_ready=1
                break
            fi
        fi
        sleep 2
    done

    if [ "$sql_ready" = "1" ]; then
        echo "  SQL Server ready. Ensuring application databases exist..."
        docker exec sqlserver /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P '8jkGh47hnDw89H@q8LN2' -C -Q "
            IF DB_ID('CustomerManagement') IS NULL CREATE DATABASE CustomerManagement;
            IF DB_ID('VehicleManagement') IS NULL CREATE DATABASE VehicleManagement;
            IF DB_ID('WorkshopManagement') IS NULL CREATE DATABASE WorkshopManagement;
            IF DB_ID('WorkshopManagementEventStore') IS NULL CREATE DATABASE WorkshopManagementEventStore;
        " >/dev/null 2>&1 || true
    else
        echo "  WARNING: SQL Server was not ready in time; continuing anyway."
    fi

    # Start the rest of the services now that SQL Server is fully up.
    echo "Starting application services..."
    docker compose -f docker-compose.yml -f docker-compose.local.yml up -d

    # Seed and test
    cd "$SCRIPT_DIR"
    bash wait-for-services.sh

    # Reset SQL databases so each run starts from the same empty state.
    echo "Resetting databases..."
    if docker exec -i sqlserver /opt/mssql-tools18/bin/sqlcmd \
        -S localhost -U sa -P '8jkGh47hnDw89H@q8LN2' -C \
        < "$SRC_DIR/scripts/ClearDatabases.sql" >/tmp/db-reset.log 2>&1; then
        echo "  Databases reset."
    else
        echo "  (DB reset skipped or failed; continuing)"
        tail -5 /tmp/db-reset.log || true
    fi

    # Purge RabbitMQ queues so no stale messages from previous runs skew results.
    echo "Purging RabbitMQ queues..."
    for queue in WorkshopManagement Invoicing Notifications CustomerManagementAcks VehicleManagementAcks WorkshopManagementAcks; do
        docker exec rabbitmq rabbitmqctl purge_queue "$queue" >/dev/null 2>&1 || true
    done

    python3 seed-sql.py --count "$COUNT"

    OUTPUT_FILE="$REPORT_DIR/perf-report-run${run}.json"
    VERIFY_FLAG=""
    if [ "${VERIFY:-0}" = "1" ]; then
        VERIFY_FLAG="--verify"
    fi
    python3 loadtest.py \
        --duration "$DURATION" \
        --concurrency "$CONCURRENCY" \
        --warmup "$WARMUP" \
        --scenario "$SCENARIO" \
        --seed-count "$COUNT" \
        --customer-api-url "http://localhost:5100" \
        --vehicle-api-url "http://localhost:5001" \
        $VERIFY_FLAG \
        --output "$OUTPUT_FILE"

    echo "Run $run report: $OUTPUT_FILE"

    # Tear down containers (preserve bind mounts for next run reset)
    cd "$SRC_DIR"
    docker compose -f docker-compose.yml -f docker-compose.local.yml down
done

echo ""
echo "Benchmark complete. Reports in $REPORT_DIR"
ls -la "$REPORT_DIR"
