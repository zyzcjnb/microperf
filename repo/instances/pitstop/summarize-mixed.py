#!/usr/bin/env python3
"""Summarize mixed-workload comparison results across Pitstop variants.

Prints:
  1. Aggregate metrics (RPS, mean latency) per variant.
  2. Per-endpoint latency table for key endpoints, with delta vs original.
"""
import json
import os
import sys

RESULTS = sys.argv[1] if len(sys.argv) > 1 else \
    "/home/zyz/microperf-benchmark/repo-clean-1/pitstop/mixed-compare-results"
BASELINE = "pitstop"

KEY_ENDPOINTS = [
    "/CustomerManagement",
    "/VehicleManagement",
    "/WorkshopManagement",
    "/api/refdata/customers",
    "/api/refdata/vehicles",
    "POST /api/customers",
    "POST /api/vehicles",
    "POST /api/workshopplanning/{date}/jobs",
    "PUT /api/workshopplanning/{date}/jobs/{jobId}/finish",
]


def load_report(variant):
    rdir = os.path.join(RESULTS, variant)
    if not os.path.isdir(rdir):
        return None
    files = sorted(f for f in os.listdir(rdir) if f.endswith(".json"))
    if not files:
        return None
    with open(os.path.join(rdir, files[0])) as f:
        return json.load(f)


def fmt(v, nd=0):
    return f"{v:.{nd}f}" if v is not None else "-"


def main():
    variants = sorted(
        d for d in os.listdir(RESULTS)
        if os.path.isdir(os.path.join(RESULTS, d))
    )
    reports = {v: load_report(v) for v in variants}
    reports = {v: r for v, r in reports.items() if r}

    print("=" * 88)
    print("AGGREGATE (whole mixed workload)")
    print("=" * 88)
    print(f"{'variant':<16}{'ok':>7}{'fail':>7}{'rps':>9}{'mean_ms':>10}{'p95_ms':>10}{'p99_ms':>10}")
    for v, r in reports.items():
        o = r.get("overall", {})
        print(f"{v:<16}{r.get('successful_requests', 0):>7}{r.get('failed_requests', 0):>7}"
              f"{r.get('rps', 0):>9}{fmt(o.get('mean')):>10}{fmt(o.get('p95')):>10}{fmt(o.get('p99')):>10}")

    base = reports.get(BASELINE, {})
    base_eps = base.get("endpoints", {})

    print()
    print("=" * 88)
    print("KEY ENDPOINT MEAN LATENCY (ms) — delta vs original pitstop")
    print("=" * 88)
    name_w = 46
    hdr = f"{'endpoint':<{name_w}}" + "".join(f"{v:>14}" for v in reports)
    print(hdr)
    print("-" * len(hdr))
    for ep in KEY_ENDPOINTS:
        row = f"{ep:<{name_w}}"
        for v, r in reports.items():
            epdata = r.get("endpoints", {}).get(ep)
            if not epdata:
                row += f"{'-':>14}"
                continue
            m = epdata["latency_ms"]["mean"]
            bm = base_eps.get(ep, {}).get("latency_ms", {}).get("mean")
            if v == BASELINE or not bm:
                row += f"{m:>12.0f}  "
            else:
                factor = m / bm if bm else 0
                row += f"{m:>8.0f} x{factor:<4.1f}"
        print(row)

    print()
    print("KEY ENDPOINT P95 LATENCY (ms)")
    print("-" * len(hdr))
    for ep in KEY_ENDPOINTS:
        row = f"{ep:<{name_w}}"
        for v, r in reports.items():
            epdata = r.get("endpoints", {}).get(ep)
            if not epdata:
                row += f"{'-':>14}"
                continue
            p = epdata["latency_ms"]["p95"]
            row += f"{p:>12.0f}  "
        print(row)


if __name__ == "__main__":
    main()
