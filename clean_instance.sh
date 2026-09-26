#!/usr/bin/env bash
# Slim a copied variant/strong-base dir: keep only what's needed to build & run.
# Usage: clean_instance.sh <dir>
set -uo pipefail
d="$1"
[ -d "$d" ] || { echo "missing: $d"; exit 1; }
sn="$d/socialNetwork"

# datasets: keep only the one the init script actually loads
find "$sn/datasets/social-graph" -mindepth 1 -maxdepth 1 -type d ! -name "socfb-Reed98" -exec rm -rf {} +

# wrk2: keep the prebuilt binary + workload scripts only
find "$d/wrk2" -mindepth 1 -maxdepth 1 ! -name wrk ! -name scripts -exec rm -rf {} +

# non-essential trees
rm -rf "$sn/figures" "$sn/gen-py" "$sn/gen-lua" "$sn/docker" "$sn/test" "$sn/keys" "$sn/helm-chart" "$sn/openshift"
rm -f "$sn/docker-compose-sharding.yml" "$sn/docker-compose-swarm.yml" "$sn/docker-compose-tls.yml" "$sn/Dockerfile-loader"

# hygiene: any stray VCS/probe state
rm -rf "$d/.git" "$d/.measurement_counter" "$d/CLAUDE.md" "$d/measure.py"

echo "cleaned $d -> $(du -sh "$d" | cut -f1)"
