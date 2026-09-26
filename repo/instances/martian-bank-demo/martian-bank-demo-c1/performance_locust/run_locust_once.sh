#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

MODE="direct"
RUN_TIME="60s"
USERS="20"
SPAWN_RATE="5"
REPORT_FILE=""

usage() {
    echo "Usage: $0 [-m MODE] [-d DURATION] [-u USERS] [-r SPAWN_RATE] [-o REPORT_FILE]"
    echo ""
    echo "Options:"
    echo "  -m MODE         direct|gateway (default: direct)"
    echo "  -d DURATION     Test duration, e.g. 60s, 2m (default: 60s)"
    echo "  -u USERS        Total concurrent users (default: 20)"
    echo "  -r SPAWN_RATE   Users spawned per second (default: 5)"
    echo "  -o REPORT_FILE  Output file name/path under performance_locust"
    echo "  -h              Show this help"
}

while getopts "m:d:u:r:o:h" opt; do
    case "$opt" in
        m) MODE="$OPTARG" ;;
        d) RUN_TIME="$OPTARG" ;;
        u) USERS="$OPTARG" ;;
        r) SPAWN_RATE="$OPTARG" ;;
        o) REPORT_FILE="$OPTARG" ;;
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

case "$MODE" in
    direct|gateway) ;;
    *)
        echo "Unsupported mode: $MODE (use direct|gateway)"
        exit 1
        ;;
esac

if [ -z "$REPORT_FILE" ]; then
    REPORT_FILE="repot_${MODE}_$(date +%Y%m%d_%H%M%S).txt"
fi

HOST_REPORT_PATH="$SCRIPT_DIR/$REPORT_FILE"
HOST_REPORT_DIR=$(dirname "$HOST_REPORT_PATH")
TMP_REPORT_BASENAME=$(basename "$REPORT_FILE")
TMP_REPORT_PATH="/tmp/$TMP_REPORT_BASENAME"

mkdir -p "$HOST_REPORT_DIR"

cd "$PROJECT_ROOT"

docker compose exec -T locust sh -lc "./locust.sh -m $MODE -d $RUN_TIME -u $USERS -r $SPAWN_RATE -o $TMP_REPORT_PATH"
docker compose cp "locust:$TMP_REPORT_PATH" "$HOST_REPORT_PATH"

echo "Report copied to: $HOST_REPORT_PATH"
