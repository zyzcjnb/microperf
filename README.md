# MicroPerf: A Benchmark for Agent-Driven Microservice Performance Optimization

MicroPerf evaluates the ability of LLM-based coding agents to find and fix
performance anti-patterns injected into microservice systems. Each instance is
a self-contained, dockerized microservice application with one or more injected
anti-patterns (e.g., chatty communication, nano-service splitting, hub
topologies, shared-database serialization, cache misuse).

## Repository layout

```
repo/
  strong-base/   # the strongest original version of each system (optimization target reference)
  instances/     # 45 instances across 5 microservice systems, each with injected anti-patterns
    hotel/               (10 variants,  Go + MongoDB/memcached, wrk2 mixed workload)
    media/               ( 7 variants,  C++ Thrift + memcached/MongoDB, wrk2 compose-review)
    martian-bank-demo/   ( 8 variants,  Python/Node.js, locust mixed workload)
    robot-shop/          (10 variants,  polyglot, locust browser-style load)
    pitstop/             (10 variants,  .NET + RabbitMQ/SQL Server, custom benchmark harness)
scripts/
  benchmark.sh                 # the measurement pipeline (3 iterations per instance)
  register_movies_for_compose.py
run-benchmark.sh               # driver: ./run-benchmark.sh [run_name]
```

In each system directory, the variant without a suffix is the unmodified
original; suffixed variants (`-c2`, `-ns`, `-c2hl`, ...) carry injected
anti-patterns. Code names: c1=chain, c2=chatty, n=nano, nl=nano-lite,
o=online, h=hub, s=shared-db (and combinations).

## Running the benchmark

Prerequisites: Docker with the compose plugin, python3, curl, ~30 GB free disk.

```bash
./run-benchmark.sh myrun
# results written to results/myrun/results.tsv
```

Per instance, the pipeline builds the stack, waits 30 s for startup, seeds data
where required, and runs the workload 3 times (60 s each). Latency is the wrk2
mean (`#[Mean = ...]`); robot-shop/martian report their load generators'
average response time.

Scoring (per instance, 0-100):

```
score = clamp( (L_injected - L_optimized) / |L_strong-base - L_injected| * 100, 0, 100 )
```

where `L_*` are mean latencies under the fixed workloads above.

## Notes

- The original `media` variant pulls the public `yg397/media-microservices`
  image; all injected variants build locally.
- robot-shop requires the public `robotshop/rs-load:2.1.0` image for the
  load generator; the pipeline tags it automatically for your `.env` TAG.
- These systems are derived from public open-source microservice benchmarks;
  please refer to the respective upstream projects for their licenses.
