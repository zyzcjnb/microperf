# MicroPerf: A Benchmark for Agent-Driven Microservice Performance Optimization

MicroPerf evaluates the ability of LLM-based coding agents to find and fix
performance anti-patterns injected into microservice systems. Each instance is
a self-contained, dockerized microservice application with one or more injected
anti-patterns (e.g., chatty communication, nano-service splitting, hub
topologies, shared-database serialization, cache misuse).

## Repository layout

```
repo/
  strong-base/             # the strongest original version of each system (optimization target reference)
  instances/               # 54 instances across 6 microservice systems, each with injected anti-patterns
    hotel/                 (10 variants,  Go + MongoDB/memcached, wrk2 mixed workload)
    media/                 ( 7 variants,  C++ Thrift + memcached/MongoDB, wrk2 compose-review)
    martian-bank-demo/     ( 8 variants,  Python/Node.js, locust mixed workload)
    robot-shop/            (10 variants,  polyglot, locust browser-style load)
    pitstop/               (10 variants,  .NET + RabbitMQ/SQL Server, custom benchmark harness)
    social/                (11 variants,  DeathStarBench social-network, mixed workload)
    spring-petclinic-micro/( 9 variants,  Java Spring Cloud + HSQLDB/MySQL, JMeter workload)

scripts/                   # per-system workload helpers
  benchmark.sh
  register_movies_for_compose.py

agents/                    # agent integration placeholders
perf_test.sh               # perf pipeline: 3 runs per target (social-network oriented)
perf_test-petclinic.sh     # perf pipeline: 10 threads x 3 runs, JMeter (spring-petclinic-micro)
test_plan.jmx              # JMeter workload plan (spring-petclinic-micro, neutralized host/port vars)
clean_instance.sh
run-benchmark.sh           # batch driver: ./run-benchmark.sh [run_name]
results/                   # perf outputs (gitignored)
```

In each system directory, the variant without a suffix is the unmodified
original; suffixed variants carry injected anti-patterns. Code names for
spring-petclinic-micro: `c1`=chain, `c2`=chatty, `c2p`=chatty-pro, `n`=nano,
`s`=shared-persistence, `l`=long-chain (combined suffixes such as `-ns`,
`-nsc2p` stack several anti-patterns). Other systems follow their own naming
documented in their directories.

## Notes

- These systems are derived from public open-source microservice benchmarks;
  please refer to the respective upstream projects for their licenses.
