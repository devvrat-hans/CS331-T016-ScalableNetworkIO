# Linux Network I/O Benchmarking Framework

A benchmarking framework for comparing four Linux network I/O mechanisms: **select, poll, epoll, and io_uring**.

The project evaluates their performance under different workloads, connection counts, traffic patterns, and threading configurations.

## Features

- Four I/O implementations: select, poll, epoll, and io_uring.
- Six application workloads: `noop`, `echo`, `upload`, `download`, `stream`, and `memory`.
- TCP and UDP support.
- Multithreaded server and load generator.
- Closed-loop and paced traffic generation.
- Configurable connection counts, buffer sizes, and request rates.
- Measurements of throughput, latency, CPU, memory, syscalls, context switches, interrupts, and softirqs.
- Automated correctness testing and benchmark analysis.

## Architecture

The project uses three layers:

1. **I/O Engines:** Implementations of select, poll, epoll, and io_uring.
2. **Common Framework:** Handles connections, request processing, TCP/UDP, threads, signals, and statistics.
3. **Application Plugins:** Implement the six supported network workloads.

All engines use the same framework and application code to ensure fair comparisons.

## Requirements

- Linux (Ubuntu 22.04 or later recommended)
- GCC / C11
- GNU Make
- Python 3
- liburing
- strace
- matplotlib
- iproute2

Install dependencies:

```bash
sudo apt update
sudo apt install build-essential make gcc pkg-config \
    liburing-dev strace python3 python3-pip iproute2

pip install matplotlib
```

## Build

```bash
cd code/benchmark
make
```

## Run

Run the complete experimental pipeline:

```bash
./run_all.sh
```

The pipeline automatically:

1. Builds all server implementations.
2. Runs correctness tests.
3. Validates syscall counters.
4. Cross-checks the load generator.
5. Executes benchmark suites.
6. Generates reports and plots.
7. Diagnoses latency spikes.

The complete experiment takes approximately five hours. Existing results can be resumed.

## Benchmark Workloads

| Workload | Purpose |
|---|---|
| noop | Baseline performance |
| echo | Latency-sensitive communication |
| upload | Read-heavy transfers |
| download | Write-heavy transfers |
| stream | Continuous data transmission |
| memory | Memory-intensive processing |

The experimental suites additionally evaluate idle connections, UDP, buffer sizes, offered load, epoll modes, SQPOLL, and multithreading.

## Measurements

The framework collects:

- Throughput (requests/second)
- Latency (p50, p90, p95, p99, p99.9)
- CPU utilization and CPU time per request
- System calls per message
- Context switches and run-queue wait
- Interrupts and softirqs
- Peak resident memory
- Socket buffer utilization

Each configuration is repeated five times. Results are aggregated using the median, with minimum and maximum values.

## Output

The experiment pipeline generates:

| File | Description |
|---|---|
| `summary.csv` | Individual benchmark results |
| `metadata.json` | Machine and experiment configuration |
| `aggregates.csv` | Aggregated measurements |
| `report.md` | Generated benchmark report |
| Plots | Performance comparison graphs |

## Validation

The framework includes automated correctness tests covering TCP, UDP, partial reads/writes, connection failures, shutdown, concurrent clients, and application workloads.

Syscall counters are validated against `strace`, and the C load generator is independently cross-checked against a Python client.
