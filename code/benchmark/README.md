# Benchmark suite

Build and run this on the Linux benchmark machine. It never changes
`SO_RCVBUF` or `SO_SNDBUF`, launches exactly one server at a time, verifies
that its PID owns port 9090, uses multiple C client processes, then stops the
server as soon as the workload completes.

```bash
cd chirag/code/benchmark
bash ./run_benchmark.sh --engine select --server ../../parth/code/select/select_echo_server \
  --connections 100 --workers 4 --messages 100 --bytes 64 --runs 5
```

Use the equivalent binary for `poll`, `epoll`, or `io_uring`; do **not** add a
port argument to the server command. Results are written to a timestamped
`results/` directory with raw client output, server logs, verified port owner,
and `summary.csv`.

For syscall profiling, make a separate run:

```bash
bash ./run_benchmark.sh --engine epoll --server ../../nachiket/code/epoll/epoll_echo_server --profile
```

`--profile` uses `strace -f -c` and changes performance, so its throughput and
latency must not be compared with normal runs. At 1000 connections, set the
open-file limit sufficiently high on both the server shell and the benchmark
shell. The runner verifies its own limit, but cannot raise it beyond the hard
limit.

The suite emits mean/min/max latency, not percentile latency. If the report
requires a latency distribution, retain raw per-request timing in a future
client revision or collect it with a dedicated latency tool. Also state that
the current engines use different write scheduling (`epoll` can write in the
read event while select/poll defer it), so syscall-per-message comparisons are
not mechanism-only comparisons.
