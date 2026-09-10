#!/usr/bin/env bash
# Repeatable, single-engine benchmark runner. Run one engine at a time.
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage: ./run_benchmark.sh --engine NAME --server '/path/to/server' [options]

Required:
  --engine NAME       select, poll, epoll, or io_uring (label in result files)
  --server COMMAND    server command; do not include a port argument

Options:
  --connections N     total simultaneous connections (default: 100)
  --workers N         independent C client processes (default: 4)
  --messages N        echo requests per connection (default: 100)
  --bytes N           bytes per echo request (default: 64)
  --runs N            measured repetitions (default: 5)
  --port N            default: 9090
  --out DIR           default: ./results/<engine>-<timestamp>
  --profile           wrap the server in strace -f -c (profiling only; do not
                      compare its timing with normal runs)
  --no-cpumem         skip the /usr/bin/time -v wrapper (bare server launch);
                      use this if /usr/bin/time is not installed
EOF
}

ENGINE= SERVER= PORT=9090 CONNECTIONS=100 WORKERS=4 MESSAGES=100 BYTES=64 RUNS=5 PROFILE=0 CPUMEM=1 OUT=
TIME_BIN=${TIME_BIN:-/usr/bin/time}
while (($#)); do
  case "$1" in
    --engine|--server|--port|--connections|--workers|--messages|--bytes|--runs|--out)
      (($# >= 2)) || { usage >&2; exit 2; }; key=${1#--}; declare "${key^^}=$2"; shift 2 ;;
    --profile) PROFILE=1; shift ;;
    --no-cpumem) CPUMEM=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done
[[ -n $ENGINE && -n $SERVER ]] || { usage >&2; exit 2; }
[[ $SERVER != *" $PORT"* ]] || { echo 'Do not pass a port to the server; all engines must use 9090.' >&2; exit 2; }
for n in "$PORT" "$CONNECTIONS" "$WORKERS" "$MESSAGES" "$BYTES" "$RUNS"; do [[ $n =~ ^[1-9][0-9]*$ ]] || { echo "positive integer required: $n" >&2; exit 2; }; done
(( WORKERS <= CONNECTIONS )) || { echo '--workers cannot exceed --connections' >&2; exit 2; }
command -v ss >/dev/null || { echo 'ss is required.' >&2; exit 1; }
command -v gcc >/dev/null || { echo 'gcc is required.' >&2; exit 1; }
[[ $PROFILE == 0 ]] || command -v strace >/dev/null || { echo 'strace is required for --profile.' >&2; exit 1; }
# CPU%/RSS wrapping only applies to normal (non --profile) runs -- nesting
# time -v inside strace would pollute the syscall counts in Table 4, so
# --profile runs stay pure strace, exactly as before.
if (( PROFILE == 0 && CPUMEM == 1 )); then
  command -v "$TIME_BIN" >/dev/null || { echo "$TIME_BIN not found; install GNU time or pass --no-cpumem." >&2; exit 1; }
fi

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
make -C "$HERE" >/dev/null
CLIENT="$HERE/tcp_echo_load"
OUT=${OUT:-"$HERE/results/${ENGINE}-$(date +%Y%m%d-%H%M%S)"}
mkdir -p "$OUT"

# Both sides need room for client sockets, the listening socket, and stdio.
needed=$(( CONNECTIONS + 128 ))
soft=$(ulimit -Sn)
if (( soft < needed )); then ulimit -n "$needed" 2>/dev/null || true; fi
if (( $(ulimit -Sn) < needed )); then echo "open-file limit $(ulimit -Sn) is below required $needed; raise ulimit -n first." >&2; exit 1; fi

if [[ $ENGINE == io_uring ]]; then
  command -v pkg-config >/dev/null || { echo 'pkg-config is required to check liburing.' >&2; exit 1; }
  liburing=$(pkg-config --modversion liburing) || { echo 'liburing is not installed.' >&2; exit 1; }
  [[ $(printf '%s\n2.2\n' "$liburing" | sort -V | head -n1) == 2.2 ]] || { echo "liburing >= 2.2 required; found $liburing." >&2; exit 1; }
  kernel=$(uname -r | sed 's/-.*//')
  [[ $(printf '%s\n5.19\n' "$kernel" | sort -V | head -n1) == 5.19 ]] || { echo "kernel >= 5.19 required for multishot accept; found $(uname -r)." >&2; exit 1; }
fi

port_listeners() { ss -H -ltnp "( sport = :$PORT )" 2>/dev/null || true; }
[[ -z $(port_listeners) ]] || { echo "port $PORT is already in use; stop its owner before starting." >&2; port_listeners >&2; exit 1; }

server_pid= port_pid=
stop_server() {
  [[ -z ${server_pid:-} ]] && return 0
  # Signal the actual listening process (port_pid) first, not the whole
  # group. For --profile (strace) and --cpumem (time -v) runs, server_pid is
  # a *wrapper* around port_pid; if the group signal reaches the wrapper at
  # the same instant as the child, the wrapper can die from the signal
  # itself before it gets to call wait4()/print its report, silently losing
  # the strace/time output. Terminating the child first lets the wrapper
  # notice a normal child exit and finish writing its report; the group
  # kill below is then just a safety net for anything left standing.
  if [[ -n ${port_pid:-} && $port_pid != "$server_pid" ]]; then
    kill -TERM "$port_pid" 2>/dev/null || true
    for _ in {1..30}; do kill -0 "$port_pid" 2>/dev/null || break; sleep 0.1; done
  fi
  # setsid gives the launch command its own process group; this cleans up
  # the wrapper (and anything else left) once the child above has exited.
  kill -TERM -- "-$server_pid" 2>/dev/null || true
  for _ in {1..50}; do kill -0 "$server_pid" 2>/dev/null || break; sleep 0.1; done
  kill -KILL -- "-$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
  server_pid= port_pid=
}
trap stop_server EXIT INT TERM

start_server() {
  [[ -z $(port_listeners) ]] || { echo "port $PORT became occupied before launch." >&2; exit 1; }
  if (( PROFILE )); then
    setsid bash -c "exec strace -f -c -o '$OUT/strace-run-$1.txt' $SERVER" >"$OUT/server-run-$1.log" 2>&1 &
  elif (( CPUMEM )); then
    # GNU time forks the server and waits on it, so it still ends up in the
    # same process group as setsid's leader; stop_server's group SIGTERM
    # reaches both, and time -v's report lands in server-run-$1.log once the
    # server exits. Overhead here is a wait4()/getrusage() call, not a
    # per-syscall trap like strace, so it is not mixed-timing the way
    # --profile is documented to be.
    setsid bash -c "exec $TIME_BIN -v $SERVER" >"$OUT/server-run-$1.log" 2>&1 &
  else
    setsid bash -c "exec $SERVER" >"$OUT/server-run-$1.log" 2>&1 &
  fi
  server_pid=$!
  for _ in {1..50}; do
    listener=$(port_listeners)
    if [[ -n $listener ]]; then
      port_pid=$(sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' <<<"$listener" | head -n1)
      # Normally the listener is the direct setsid child.  With --profile it
      # is strace's child, which is still acceptable and is recorded.
      if [[ $port_pid == "$server_pid" || $(ps -o ppid= -p "$port_pid" | tr -d ' ') == "$server_pid" ]]; then
        printf '%s\n' "$listener" >"$OUT/port-owner-run-$1.txt"
        return 0
      fi
    fi
    kill -0 "$server_pid" 2>/dev/null || { echo "server exited before binding port $PORT; see server-run-$1.log" >&2; exit 1; }
    sleep 0.1
  done
  echo "server did not become the verified owner of port $PORT." >&2; port_listeners >&2; exit 1
}

echo 'run,engine,connections,workers,messages_per_connection,bytes_per_message,attempted,completed,bytes,elapsed_ns,throughput_msg_s,throughput_mib_s,mean_latency_us,min_latency_us,max_latency_us,server_threads,cpu_percent,max_rss_kb' >"$OUT/summary.csv"
for run in $(seq 1 "$RUNS"); do
  start_server "$run"
  threads=$(find "/proc/$port_pid/task" -mindepth 1 -maxdepth 1 -type d | wc -l)
  [[ $ENGINE != io_uring || $threads == 1 ]] || echo "WARNING: io_uring has $threads threads; record this caveat." >&2
  base=$(( CONNECTIONS / WORKERS )); remainder=$(( CONNECTIONS % WORKERS ))
  pids=(); files=()
  for worker in $(seq 0 $((WORKERS - 1))); do
    count=$base; (( worker < remainder )) && ((count++))
    file="$OUT/client-run-$run-worker-$worker.txt"; files+=("$file")
    "$CLIENT" --port "$PORT" --connections "$count" --messages "$MESSAGES" --bytes "$BYTES" >"$file" 2>"$file.err" & pids+=("$!")
  done
  failed=0; for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
  stop_server
  (( failed == 0 )) || { echo "client workload failed in run $run; inspect $OUT." >&2; exit 1; }
  # Sum nanosecond counters from key=value output without rounding each worker.
  values=$(awk '{for(i=1;i<=NF;i++){split($i,a,"="); v[a[1]]+=a[2]}} END {printf "%.0f %.0f %.0f %.0f %.0f",v["attempted"],v["completed"],v["bytes"],v["elapsed_ns"],v["latency_total_ns"]}' "${files[@]}")
  read -r attempted completed total_bytes elapsed latency_total <<<"$values"
  latency_min=$(awk '{for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]=="latency_min_ns" && (!seen || a[2]<m)){m=a[2]; seen=1}}} END {printf "%.0f",m}' "${files[@]}")
  latency_max=$(awk '{for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]=="latency_max_ns" && a[2]>m)m=a[2]}} END {printf "%.0f",m}' "${files[@]}")
  # Each worker runs independently, so wall time is the maximum worker elapsed time.
  wall_ns=$(awk '{for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]=="elapsed_ns" && a[2]>m)m=a[2]}} END {printf "%.0f",m}' "${files[@]}")
  # Pulled from GNU time -v's report in server-run-$run.log (empty when
  # --profile or --no-cpumem was used, so the column stays but is blank).
  cpu_percent=$(sed -n 's/.*Percent of CPU this job got: \([0-9]*\)%.*/\1/p' "$OUT/server-run-$run.log" | head -n1)
  max_rss_kb=$(sed -n 's/.*Maximum resident set size (kbytes): \([0-9]*\).*/\1/p' "$OUT/server-run-$run.log" | head -n1)
  awk -v r="$run" -v e="$ENGINE" -v c="$CONNECTIONS" -v w="$WORKERS" -v m="$MESSAGES" -v b="$BYTES" -v a="$attempted" -v done="$completed" -v bytes="$total_bytes" -v wall="$wall_ns" -v lsum="$latency_total" -v lmin="$latency_min" -v lmax="$latency_max" -v t="$threads" -v cpu="$cpu_percent" -v rss="$max_rss_kb" 'BEGIN {printf "%d,%s,%d,%d,%d,%d,%.0f,%.0f,%.0f,%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%s,%s\n",r,e,c,w,m,b,a,done,bytes,wall,done/(wall/1e9),(bytes/1048576)/(wall/1e9),(lsum/done)/1000,lmin/1000,lmax/1000,t,cpu,rss}' >>"$OUT/summary.csv"
done
echo "Saved results to $OUT/summary.csv"
