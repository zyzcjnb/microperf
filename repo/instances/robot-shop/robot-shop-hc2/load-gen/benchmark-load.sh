#!/bin/sh

set -e

NUM_CLIENTS=10
RUN_TIME=10m
HOST="http://localhost:8080"
ERROR=0
RESULT_DIR="$(pwd)/results"
RESULT_PREFIX="benchmark"

USAGE="
benchmark-load.sh

e - enable error injection
n - number of clients
t - benchmark duration (e.g. 10m, 1h30m)
h - target host
o - output directory for CSV/JSON summary
p - result file prefix
"

if [ ! -f ../.env ]
then
    echo "Please run this script from the load-gen directory"
    exit 1
fi

eval $(egrep '[A-Z]+=' ../.env)

while getopts 'en:t:h:o:p:' OPT
do
    case $OPT in
        e)
            ERROR=1
            ;;
        n)
            NUM_CLIENTS=$OPTARG
            ;;
        t)
            RUN_TIME=$OPTARG
            ;;
        h)
            HOST=$OPTARG
            ;;
        o)
            RESULT_DIR=$OPTARG
            ;;
        p)
            RESULT_PREFIX=$OPTARG
            ;;
        *)
            echo "$USAGE"
            exit 1
            ;;
    esac
done

mkdir -p "$RESULT_DIR"

echo "Repo $REPO"
echo "Tag $TAG"
echo "Result dir $RESULT_DIR"

docker run \
    --rm \
    --network=host \
    -v "$RESULT_DIR:/results" \
    -e "HOST=$HOST" \
    -e "NUM_CLIENTS=$NUM_CLIENTS" \
    -e "RUN_TIME=$RUN_TIME" \
    -e "ERROR=$ERROR" \
    -e "RESULT_DIR=/results" \
    -e "RESULT_PREFIX=$RESULT_PREFIX" \
    ${REPO}/rs-load:${TAG} \
    ./benchmark-entrypoint.sh

echo "Benchmark completed. Summary: $RESULT_DIR/${RESULT_PREFIX}_summary.json"