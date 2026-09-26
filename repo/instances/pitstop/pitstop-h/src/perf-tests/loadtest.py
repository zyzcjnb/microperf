#!/usr/bin/env python3
"""HTTP load test for Pitstop with multiple scenarios.

Scenarios:
  read/c2  - mixed read-only traffic through the WebApp
  c1        - direct calls to WorkshopManagement refdata endpoints
  write     - event-generating writes
  mixed     - unified mixed workload
"""
import argparse
import json
import random
import statistics
import string
import subprocess
import sys
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from datetime import date, datetime, timedelta
from typing import Callable, List, Tuple

import requests


@dataclass
class RequestResult:
    status_code: int
    elapsed_ms: float
    endpoint: str
    error: str = ""


@dataclass
class EndpointStats:
    count: int = 0
    success: int = 0
    errors: int = 0
    latencies_ms: List[float] = field(default_factory=list)

    def add(self, result: RequestResult):
        self.count += 1
        if result.error or result.status_code >= 400:
            self.errors += 1
        else:
            self.success += 1
        if not result.error:
            self.latencies_ms.append(result.elapsed_ms)


def percentile(sorted_values: List[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    k = (len(sorted_values) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(sorted_values) - 1)
    if f == c:
        return sorted_values[f]
    return sorted_values[f] * (c - k) + sorted_values[c] * (k - f)


def seeded_customer_id(index: int) -> str:
    return f"CUST{index:05d}"


def seeded_license(index: int) -> str:
    return f"{index:03d}-{index:03d}-{index:03d}"


class LoadTest:
    def __init__(
        self,
        base_url: str,
        api_url: str,
        customer_api_url: str,
        vehicle_api_url: str,
        duration: int,
        concurrency: int,
        warmup: int,
        scenario: str,
        seed_count: int,
        verify: bool,
    ):
        self.base_url = base_url.rstrip("/")
        self.api_url = api_url.rstrip("/")
        self.customer_api_url = customer_api_url.rstrip("/")
        self.vehicle_api_url = vehicle_api_url.rstrip("/")
        self.duration = duration
        self.concurrency = concurrency
        self.warmup = warmup
        self.scenario = scenario
        self.seed_count = seed_count
        self.verify = verify
        self.results_lock = threading.Lock()
        self.results: List[RequestResult] = []
        self.stop_event = threading.Event()

    def _label(self, method: str, path: str) -> str:
        return f"{method} {path}"

    def _make_request(
        self, session: requests.Session, method: str, url: str, label: str, json_body=None
    ) -> RequestResult:
        start = time.perf_counter()
        try:
            if method == "GET":
                r = session.get(url, timeout=30, headers={"Cache-Control": "no-cache"})
            elif method == "POST":
                r = session.post(
                    url,
                    json=json_body,
                    timeout=30,
                    headers={"Cache-Control": "no-cache"},
                )
            elif method == "PUT":
                r = session.put(
                    url,
                    json=json_body,
                    timeout=30,
                    headers={"Cache-Control": "no-cache"},
                )
            else:
                raise ValueError(f"Unsupported method: {method}")
            elapsed_ms = (time.perf_counter() - start) * 1000
            return RequestResult(
                status_code=r.status_code, elapsed_ms=elapsed_ms, endpoint=label
            )
        except Exception as ex:
            elapsed_ms = (time.perf_counter() - start) * 1000
            return RequestResult(
                status_code=0, elapsed_ms=elapsed_ms, endpoint=label, error=str(ex)
            )

    def _read_customer_management(self, session: requests.Session) -> None:
        url = f"{self.base_url}/CustomerManagement"
        self._record(self._make_request(session, "GET", url, "/CustomerManagement"))

    def _read_vehicle_management(self, session: requests.Session) -> None:
        url = f"{self.base_url}/VehicleManagement"
        self._record(self._make_request(session, "GET", url, "/VehicleManagement"))

    def _read_workshop_management(self, session: requests.Session) -> None:
        url = f"{self.base_url}/WorkshopManagement"
        self._record(self._make_request(session, "GET", url, "/WorkshopManagement"))

    def _read_refdata_customers(self, session: requests.Session) -> None:
        url = f"{self.api_url}/api/refdata/customers"
        self._record(self._make_request(session, "GET", url, "/api/refdata/customers"))

    def _read_refdata_vehicles(self, session: requests.Session) -> None:
        url = f"{self.api_url}/api/refdata/vehicles"
        self._record(self._make_request(session, "GET", url, "/api/refdata/vehicles"))

    def _write_customer(self, session: requests.Session, worker_id: int, counter: int) -> None:
        customer_id = f"CUST-W{worker_id}-{counter}-{uuid.uuid4().hex[:6]}"
        body = {
            "MessageId": str(uuid.uuid4()),
            "CustomerId": customer_id,
            "Name": f"Perf Customer {worker_id}-{counter}",
            "Address": f"Street {worker_id}-{counter}",
            "PostalCode": f"{10000 + (counter % 90000):05d}",
            "City": "Test City",
            "TelephoneNumber": f"555-{worker_id:05d}-{counter:05d}",
            "EmailAddress": f"cust{worker_id}-{counter}@pitstop.perf",
        }
        url = f"{self.customer_api_url}/api/customers"
        self._record(self._make_request(session, "POST", url, "POST /api/customers", body))

    def _write_vehicle(self, session: requests.Session, worker_id: int, counter: int) -> None:
        owner_id = seeded_customer_id(random.randint(0, self.seed_count - 1))
        license_letters = "".join(random.choices(string.ascii_lowercase, k=3))
        license_number = f"{worker_id:03d}-{counter:03d}-{license_letters}"
        body = {
            "MessageId": str(uuid.uuid4()),
            "LicenseNumber": license_number,
            "Brand": random.choice(["Toyota", "Honda", "Ford", "BMW", "Audi"]),
            "Type": random.choice(["Sedan", "SUV", "Hatchback"]),
            "OwnerId": owner_id,
        }
        url = f"{self.vehicle_api_url}/api/vehicles"
        self._record(self._make_request(session, "POST", url, "POST /api/vehicles", body))

    def _write_job_cycle(self, session: requests.Session, worker_id: int, counter: int) -> None:
        # Spread workers across different days and sequential minute-level
        # time-slots so each job gets a unique non-overlapping slot. This avoids
        # workstation business-rule conflicts (max 3 parallel jobs per day) even
        # under high throughput.
        planning_date_obj = date.today() + timedelta(days=worker_id % 1000)
        planning_date = planning_date_obj.isoformat()
        job_id = str(uuid.uuid4())
        slot_minute = counter % (14 * 60)
        start = (
            datetime.combine(planning_date_obj, datetime.min.time())
            + timedelta(hours=8, minutes=slot_minute)
        )
        end = start + timedelta(minutes=1)

        cust_idx = random.randint(0, self.seed_count - 1)
        veh_idx = random.randint(0, self.seed_count - 1)

        plan_body = {
            "MessageId": str(uuid.uuid4()),
            "JobId": job_id,
            "StartTime": start.isoformat(),
            "EndTime": end.isoformat(),
            "CustomerInfo": {
                "Id": seeded_customer_id(cust_idx),
                "Name": f"Customer {cust_idx}",
                "TelephoneNumber": f"555-{cust_idx:05d}",
            },
            "VehicleInfo": {
                "LicenseNumber": seeded_license(veh_idx),
                "Brand": "Brand",
                "Type": "Type",
            },
            "Description": f"Perf job {worker_id}-{counter}",
        }
        plan_url = f"{self.api_url}/api/workshopplanning/{planning_date}/jobs"
        plan_result = self._make_request(
            session, "POST", plan_url, "POST /api/workshopplanning/{date}/jobs", plan_body
        )
        self._record(plan_result)

        if not (plan_result.status_code and 200 <= plan_result.status_code < 300):
            return

        finish_body = {
            "MessageId": str(uuid.uuid4()),
            "JobId": job_id,
            "StartTime": start.isoformat(),
            "EndTime": end.isoformat(),
            "Notes": "done",
        }
        finish_url = f"{self.api_url}/api/workshopplanning/{planning_date}/jobs/{job_id}/finish"
        self._record(
            self._make_request(
                session,
                "PUT",
                finish_url,
                "PUT /api/workshopplanning/{date}/jobs/{jobId}/finish",
                finish_body,
            )
        )

    def _record(self, result: RequestResult) -> None:
        with self.results_lock:
            self.results.append(result)

    def _worker(self, worker_id: int):
        session = requests.Session()
        counter = 0

        if self.scenario == "c1":
            operations = [
                (self._read_refdata_customers, 0.45),
                (self._read_refdata_vehicles, 0.45),
                (self._read_workshop_management, 0.10),
            ]
        elif self.scenario == "mixed":
            operations = [
                (self._read_customer_management, 0.10),
                (self._read_vehicle_management, 0.05),
                (self._read_workshop_management, 0.05),
                (self._read_refdata_customers, 0.21),
                (self._read_refdata_vehicles, 0.21),
                (lambda s: self._write_customer(s, worker_id, counter), 0.185),
                (lambda s: self._write_vehicle(s, worker_id, counter), 0.185),
                (lambda s: self._write_job_cycle(s, worker_id, counter), 0.01),
            ]
        elif self.scenario == "write":
            operations = [
                (lambda s: self._write_customer(s, worker_id, counter), 0.25),
                (lambda s: self._write_vehicle(s, worker_id, counter), 0.25),
                (lambda s: self._write_job_cycle(s, worker_id, counter), 0.50),
            ]
        else:
            operations = [
                (self._read_customer_management, 0.45),
                (self._read_vehicle_management, 0.45),
                (self._read_workshop_management, 0.10),
            ]

        funcs = [op[0] for op in operations]
        weights = [op[1] for op in operations]

        while not self.stop_event.is_set():
            counter += 1
            fn = random.choices(funcs, weights=weights, k=1)[0]
            fn(session)

    def _count_workshop_jobs(self) -> int:
        try:
            cmd = [
                "docker", "exec", "sqlserver",
                "/opt/mssql-tools18/bin/sqlcmd",
                "-S", "localhost",
                "-U", "sa",
                "-P", "8jkGh47hnDw89H@q8LN2",
                "-C",
                "-h", "-1", "-W",
                "-Q", "SET NOCOUNT ON; SELECT COUNT(*) FROM WorkshopManagement.dbo.MaintenanceJob;",
            ]
            result = subprocess.run(cmd, text=True, capture_output=True, timeout=10)
            if result.returncode == 0:
                return int(result.stdout.strip().splitlines()[-1].strip())
        except Exception:
            pass
        return -1

    def _verify_consumer_processing(self, expected_jobs: int) -> dict:
        if expected_jobs <= 0:
            return {}
        print(f"Waiting for consumers to process {expected_jobs} jobs...")
        start = time.time()
        actual = 0
        timeout = 300
        while time.time() - start < timeout:
            actual = self._count_workshop_jobs()
            if actual >= expected_jobs:
                break
            time.sleep(2)
        elapsed = time.time() - start
        return {
            "expected_jobs": expected_jobs,
            "actual_jobs": actual,
            "consumer_processing_time_seconds": round(elapsed, 2),
            "timed_out": actual < expected_jobs,
        }

    def _run_phase(self, phase_name: str, duration: int, collect: bool = True) -> dict:
        print(f"Starting {phase_name}: {duration}s with {self.concurrency} concurrent users...")
        self.stop_event.clear()
        start_time = time.time()

        with ThreadPoolExecutor(max_workers=self.concurrency) as executor:
            futures = [executor.submit(self._worker, i) for i in range(self.concurrency)]
            time.sleep(duration)
            self.stop_event.set()
            for future in futures:
                try:
                    future.result(timeout=10)
                except Exception:
                    pass

        elapsed = time.time() - start_time
        if collect:
            stats = self._compute_stats(elapsed)
            if self.scenario == "write" and self.verify:
                expected_jobs = sum(
                    1 for r in self.results
                    if r.endpoint == "POST /api/workshopplanning/{date}/jobs"
                    and not r.error and 200 <= r.status_code < 300
                )
                stats["consumer_processing"] = self._verify_consumer_processing(expected_jobs)
            return stats
        else:
            with self.results_lock:
                self.results = []
            return {}

    def _compute_stats(self, elapsed: float) -> dict:
        with self.results_lock:
            results = list(self.results)

        endpoint_stats: dict[str, EndpointStats] = {}
        for r in results:
            stats = endpoint_stats.setdefault(r.endpoint, EndpointStats())
            stats.add(r)

        overall_latencies = sorted([r.elapsed_ms for r in results if not r.error])
        total_requests = len(results)
        total_success = sum(s.success for s in endpoint_stats.values())
        total_errors = sum(s.errors for s in endpoint_stats.values())

        report = {
            "total_requests": total_requests,
            "successful_requests": total_success,
            "failed_requests": total_errors,
            "duration_seconds": elapsed,
            "rps": round(total_requests / elapsed, 2) if elapsed > 0 else 0,
            "overall": self._latency_summary(overall_latencies),
            "endpoints": {},
        }

        for endpoint, stats in sorted(endpoint_stats.items()):
            report["endpoints"][endpoint] = {
                "count": stats.count,
                "success": stats.success,
                "errors": stats.errors,
                "rps": round(stats.count / elapsed, 2) if elapsed > 0 else 0,
                "latency_ms": self._latency_summary(sorted(stats.latencies_ms)),
            }

        return report

    def _latency_summary(self, latencies: List[float]) -> dict:
        if not latencies:
            return {"min": 0, "max": 0, "mean": 0, "p50": 0, "p95": 0, "p99": 0}
        return {
            "min": round(min(latencies), 2),
            "max": round(max(latencies), 2),
            "mean": round(statistics.mean(latencies), 2),
            "p50": round(percentile(latencies, 50), 2),
            "p95": round(percentile(latencies, 95), 2),
            "p99": round(percentile(latencies, 99), 2),
        }

    def run(self) -> dict:
        if self.warmup > 0:
            self._run_phase("warmup", self.warmup, collect=False)
        return self._run_phase("load test", self.duration, collect=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://localhost:7005")
    parser.add_argument("--api-url", default="http://localhost:5200")
    parser.add_argument("--customer-api-url", default="http://localhost:5100")
    parser.add_argument("--vehicle-api-url", default="http://localhost:5001")
    parser.add_argument("--duration", type=int, default=60, help="Test duration in seconds")
    parser.add_argument("--concurrency", type=int, default=30, help="Number of concurrent users")
    parser.add_argument("--warmup", type=int, default=15, help="Warmup duration in seconds")
    parser.add_argument(
        "--scenario",
        default="c2",
        choices=["read", "c2", "c1", "write", "mixed"],
        help="Test scenario",
    )
    parser.add_argument("--seed-count", type=int, default=500, help="Number of seeded customers/vehicles")
    parser.add_argument("--verify", action="store_true", help="After the write run, measure consumer processing time")
    parser.add_argument("--output", default="perf-report.json", help="Output JSON file")
    args = parser.parse_args()

    scenario = "read" if args.scenario == "c2" else args.scenario

    tester = LoadTest(
        args.base_url,
        args.api_url,
        args.customer_api_url,
        args.vehicle_api_url,
        args.duration,
        args.concurrency,
        args.warmup,
        scenario,
        args.seed_count,
        args.verify,
    )
    report = tester.run()

    report["config"] = {
        "base_url": args.base_url,
        "api_url": args.api_url,
        "customer_api_url": args.customer_api_url,
        "vehicle_api_url": args.vehicle_api_url,
        "duration_seconds": args.duration,
        "concurrency": args.concurrency,
        "warmup_seconds": args.warmup,
        "scenario": scenario,
        "seed_count": args.seed_count,
        "verify": args.verify,
    }

    with open(args.output, "w") as f:
        json.dump(report, f, indent=2)

    print(json.dumps(report, indent=2))
    print(f"\nReport saved to {args.output}")


if __name__ == "__main__":
    main()
