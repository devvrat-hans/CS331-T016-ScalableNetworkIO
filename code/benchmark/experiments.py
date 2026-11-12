#!/usr/bin/env python3
"""Experiment framework: define, run, validate and analyse the whole study.

    python3 experiments.py list
    python3 experiments.py run --out results/final
    python3 experiments.py analyze --input results/final --out results/final/report
    python3 experiments.py check-counters --out results/counter-check

An experiment is only data (a SUITES entry). Network applications are plugins
selected by name, so adding one means adding a table in common/apps.c (Chirag/code) and a
suite here; nothing in this runner or in any engine is specific to one of them.
"""
import argparse
import collections
import csv
import json
import os
import platform
import random
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # Chirag/code: bin/ and build/ live here
BASE = ["select", "poll", "epoll", "io_uring"]
ALL_VARIANTS = BASE + ["epoll-et", "epoll-lt-direct", "io_uring-sqpoll", "io_uring-sqpoll-busy"]
# io_uring keeps --udp-depth receives waiting on the one UDP socket. No single
# depth is right (1 cannot pipeline, 32 wakes many receives per datagram), so
# UDP suites measure several instead of choosing one.
UDP_VARIANTS = ["select", "poll", "epoll", "io_uring-d1", "io_uring-d4", "io_uring-d32"]
# Readiness engines with --send-mode full: on a writable event they keep sending
# until the reply is done or the socket is full, instead of one send per event.
FULL_VARIANTS = ["select-full", "poll-full", "epoll-full"]
PLOT_ORDER = ALL_VARIANTS + FULL_VARIANTS + ["io_uring-d1", "io_uring-d4", "io_uring-d32"]
DEFAULT_BUFFER = 4096
KIB = 1024
# Up to eight client threads, on the spare cores and their hyperthreads; four
# saturated against a four-thread server on the first run.
CLIENT_THREADS = 8
# The controller (this script) runs on its own CPU. Unpinned, it and other
# tasks were placed on the server's core while a paced client kept its cores
# busy, and the server waited for its CPU: the source of the p99 spikes.
# Read once, before the controller pins itself to CONTROLLER_CPU.
ALLOWED_CPUS = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else list(range(os.cpu_count()))
CONTROLLER_CPU = ALLOWED_CPUS[-1]
# Paced clients sleep until just before each send instead of spinning (see above).
PACE = "hybrid"
# CPU 0 usually carries more of the machine's own work (timers, kernel workers,
# device interrupts). In the full run the SQPOLL tail followed the app thread's
# run-queue wait on CPU 0, so the server now gets other cores when there are enough.
AVOID_CPU0 = True
MIB = 1024 * KIB

# Why each suite exists is part of the experiment definition.
SUITES = {
    "noop-baseline": dict(
        purpose="Sir's baseline: the thread does nothing, giving the top operating frequency of each loop.",
        app="noop", size=1, x="conns", conns=[1, 10, 100]),
    "echo-scaling": dict(
        purpose="Latency-sensitive small messages (chat-like) with every connection active.",
        app="echo", size=64, x="conns", conns=[10, 100, 500, 1000]),
    "idle-scaling": dict(
        purpose="Ten active clients among many idle descriptors: isolates the O(N) scan from useful work.",
        app="echo", size=64, x="conns", conns=[10, 100, 500, 1000], active=10),
    "upload": dict(
        purpose="Read-intensive bulk transfer (file upload).",
        app="upload", size=MIB, x="conns", conns=[10, 100]),
    "download": dict(
        purpose="Write-intensive bulk transfer (file download). *-full variants send until the socket is full.",
        app="download", size=MIB, x="conns", conns=[10, 100], variants=BASE + FULL_VARIANTS),
    "stream": dict(
        purpose="Continuous paced frames (video-like): 64 KiB frames at 30 per second per connection. "
                "*-full variants send until the socket is full.",
        app="stream", size=64 * KIB, x="conns", conns=[10, 100], rate_per_conn=30,
        variants=BASE + FULL_VARIANTS),
    "memory": dict(
        purpose="Memory-intensive: every client owns a resident 256 KiB buffer; each request is written "
                "after the previous one, so 500 clients cycle through 125 MiB (beyond the CPU caches).",
        app="memory", size=4 * KIB, x="conns", conns=[10, 100, 500]),
    "udp-echo-64": dict(
        purpose="UDP small datagrams. One server socket, so there is nothing per client to scan.",
        app="echo", size=64, transport="udp", x="conns", conns=[10, 100], variants=UDP_VARIANTS),
    "udp-echo-1400": dict(
        purpose="UDP near the Ethernet MTU, unfragmented.",
        app="echo", size=1400, transport="udp", x="conns", conns=[10, 100], variants=UDP_VARIANTS),
    "udp-echo-4096": dict(
        purpose="UDP above the Ethernet MTU, the size that would be IP-fragmented on a real network.",
        app="echo", size=4096, transport="udp", x="conns", conns=[10, 100], variants=UDP_VARIANTS),
    "udp-stream": dict(
        purpose="UDP media-like traffic: 1400-byte frames at 30 per second per flow.",
        app="stream", size=1400, transport="udp", x="conns", conns=[10, 100], rate_per_conn=30, variants=UDP_VARIANTS),
    "rate-sweep": dict(
        purpose="TA feedback: does CPU follow offered load? Fixed connections, increasing paced request rate.",
        app="echo", size=64, x="rate", conns=[100], rates=[1000, 5000, 10000, 20000, 40000],
        variants=ALL_VARIANTS),
    "epoll-lt-et": dict(
        purpose="TA extension: level-triggered versus edge-triggered epoll.",
        app="echo", size=64, x="conns", conns=[10, 100, 1000], variants=["epoll", "epoll-et", "epoll-lt-direct"]),
    "epoll-lt-et-download": dict(
        purpose="TA extension: LT versus ET where one wakeup can move many bytes.",
        app="download", size=MIB, x="conns", conns=[10, 100], variants=["epoll", "epoll-et", "epoll-lt-direct"]),
    "sqpoll": dict(
        purpose="TA extension: io_uring with and without a submission-queue polling kernel thread.",
        app="echo", size=64, x="conns", conns=[10, 100, 500],
        variants=["io_uring", "io_uring-sqpoll", "io_uring-sqpoll-busy"]),
    "multithread": dict(
        purpose="TA extension and Sir's scaling dimension: independent event loops, SO_REUSEPORT.",
        app="echo", size=64, x="threads", conns=[1000], threads=[1, 2, 4]),
    "buffer-download": dict(
        purpose="Server buffer size: 1 MiB downloads moved in 4, 16 or 64 KiB pieces (fewer system calls per MiB).",
        app="download", size=MIB, x="buffer", conns=[10], buffers=[4096, 16384, 65536]),
    "buffer-upload": dict(
        purpose="Server buffer size for 1 MiB uploads.",
        app="upload", size=MIB, x="buffer", conns=[10], buffers=[4096, 16384, 65536]),
}


def physical_cpus():
    """One logical CPU per physical core, so a server never shares with an SMT sibling."""
    allowed = ALLOWED_CPUS
    seen, chosen = set(), []
    for cpu in allowed:
        path = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology/core_id")
        package = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology/physical_package_id")
        try:
            key = (package.read_text().strip(), path.read_text().strip())
        except OSError:
            key = cpu
        if key not in seen:
            seen.add(key)
            chosen.append(cpu)
    return chosen


def core_of(cpu):
    base = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
    try:
        return ((base / "physical_package_id").read_text().strip(), (base / "core_id").read_text().strip())
    except OSError:
        return cpu


def place(threads, sqpoll, conns):
    """Server threads (and the SQPOLL thread) get whole physical cores. The
    client uses the remaining cores, first one CPU per core, then their
    hyperthread siblings, so it never shares a core with the server."""
    cpus = physical_cpus()
    housekeeping = core_of(0)
    if AVOID_CPU0 and 0 in ALLOWED_CPUS:
        # Core 0 goes last, and the client avoids it too while it has enough other CPUs.
        cpus = [c for c in cpus if core_of(c) != housekeeping] + [c for c in cpus if core_of(c) == housekeeping]
    server = cpus[:threads]
    rest = cpus[threads:]
    sq_cpu = None
    if sqpoll:
        if not rest:
            raise RuntimeError("no spare core for the SQPOLL thread")
        sq_cpu, rest = rest[0], rest[1:]
    if not rest:
        raise RuntimeError("no core left for the load generator")
    taken = {core_of(c) for c in server + ([sq_cpu] if sq_cpu is not None else [])}
    siblings = [c for c in ALLOWED_CPUS if core_of(c) not in taken and c not in rest]
    spare = [c for c in rest + siblings if c != CONTROLLER_CPU]
    wanted = min(CLIENT_THREADS, conns)
    if AVOID_CPU0:
        away = [c for c in spare if core_of(c) != housekeeping]
        if len(away) >= wanted:
            spare = away
    client = spare[:wanted]
    if not client:
        raise RuntimeError("no core left for the load generator")
    return server, sq_cpu, client


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return "unavailable"


def environment():
    """Settings that changed results on the first run, recorded with the data."""
    governors = sorted({read(p) for p in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_governor")})
    preference = sorted({read(p) for p in Path("/sys/devices/system/cpu").glob(
        "cpu[0-9]*/cpufreq/energy_performance_preference")})
    mains = [read(p.parent / "online") for p in Path("/sys/class/power_supply").glob("*/type") if read(p) == "Mains"]
    return dict(governors=governors, energy_preference=preference, on_mains_power=mains,
                rmem_max=read("/proc/sys/net/core/rmem_max"), wmem_max=read("/proc/sys/net/core/wmem_max"))


def kernel_accounting():
    """How the kernel accounts CPU time, which decides how to read the core columns.
    With CONFIG_IRQ_TIME_ACCOUNTING=y softirq/irq time in /proc/stat is exact and is
    NOT included in the process's CPU time; user/system per core are tick-sampled."""
    wanted = ("CONFIG_IRQ_TIME_ACCOUNTING", "CONFIG_VIRT_CPU_ACCOUNTING_GEN", "CONFIG_HZ", "CONFIG_NO_HZ_IDLE",
              "CONFIG_NO_HZ_FULL")
    found = {}
    for line in read(f"/boot/config-{platform.release()}").splitlines():
        key, _, value = line.partition("=")
        if key in wanted:
            found[key] = value
    return found or "unavailable"


def proc_snapshot(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    voluntary = involuntary = runqueue = kernel_runqueue = 0
    tasks = 0
    for task in Path(f"/proc/{pid}/task").iterdir():
        try:
            status = (task / "status").read_text()
        except OSError:
            continue
        tasks += 1
        voluntary += int(re.search(r"voluntary_ctxt_switches:\s+(\d+)", status).group(1))
        involuntary += int(re.search(r"nonvoluntary_ctxt_switches:\s+(\d+)", status).group(1))
        # Time spent runnable but waiting for a CPU (ns): high values mean
        # another task was using the server's core.
        parts = read(task / "schedstat").split()
        wait = int(parts[1]) if len(parts) > 1 else 0
        runqueue += wait
        # io_uring's kernel threads (iou-sqp SQPOLL, iou-wrk workers) belong to
        # the process; separate them from our own event-loop thread.
        if read(task / "comm").startswith("iou-"):
            kernel_runqueue += wait
    return {"utime": int(fields[11]), "stime": int(fields[12]), "voluntary": voluntary,
            "involuntary": involuntary, "tasks": tasks, "runqueue": runqueue,
            "kernel_runqueue": kernel_runqueue, "time": time.perf_counter()}


def softirq_snapshot(cpus):
    """Network softirqs and hardware interrupts on the given CPUs."""
    result = dict(NET_RX=0, NET_TX=0, SCHED=0, HARD=0, LOC=0, RES=0, CAL=0, DEV=0, OTHER=0)
    try:
        lines = Path("/proc/softirqs").read_text().splitlines()
        interrupts = Path("/proc/interrupts").read_text().splitlines()
    except OSError:
        return result
    if not lines or not interrupts:  # Containers hide these files; real Linux has them.
        return result
    names = lines[0].split()
    for line in lines[1:]:
        label, *values = line.split()
        if label in ("NET_RX:", "NET_TX:", "SCHED:"):
            result[label[:-1]] = sum(int(values[names.index(f"CPU{c}")]) for c in cpus)
    total = 0
    lines = interrupts
    names = lines[0].split()
    positions = [names.index(f"CPU{c}") for c in cpus if f"CPU{c}" in names]
    for line in lines[1:]:
        label, *parts = line.split()
        count = sum(int(parts[p]) for p in positions if p < len(parts) and parts[p].isdigit())
        total += count
        # Numbered rows are devices; named rows are CPU-internal interrupts:
        # LOC local timer, RES reschedule (one CPU waking a task on another),
        # CAL function call between CPUs.
        kind = label.rstrip(":")
        if kind.isdigit():
            result["DEV"] += count
        elif kind in ("LOC", "RES", "CAL"):
            result[kind] += count
        else:
            result["OTHER"] += count
    result["HARD"] = total
    return result


def core_times(cpus):
    """Per-core time from /proc/stat (ticks), summed over the server's cores.
    The process CPU above only counts what the kernel charges to our threads;
    network softirqs and interrupts handled on the same core may be accounted
    separately (CONFIG_IRQ_TIME_ACCOUNTING), so this shows all work on the core."""
    total = dict(busy=0, softirq=0, irq=0, all=0)
    for line in read("/proc/stat").splitlines():
        parts = line.split()
        if not parts or not parts[0].startswith("cpu") or parts[0] == "cpu":
            continue
        if int(parts[0][3:]) not in cpus:
            continue
        values = [int(v) for v in parts[1:9]]  # user nice system idle iowait irq softirq steal
        total["all"] += sum(values)
        total["busy"] += sum(values) - values[3] - values[4]
        total["irq"] += values[5]
        total["softirq"] += values[6]
    return total


def sockstat_kib():
    """Kernel memory held by all TCP and UDP sockets on the machine (client too)."""
    pages = 0
    for line in read("/proc/net/sockstat").splitlines():
        parts = line.split()
        if parts and parts[0] in ("TCP:", "UDP:") and "mem" in parts:
            pages += int(parts[parts.index("mem") + 1])
    return pages * os.sysconf("SC_PAGE_SIZE") // 1024


def server_socket_kib(port, transport):
    """Kernel memory queued on the SERVER's sockets (local port = port): bytes
    received but not yet read plus bytes queued for sending, from ss -m (skmem
    r and w). Client sockets are excluded. None if ss is not available."""
    tool = shutil.which("ss")
    if not tool:
        return None
    flag = "-u" if transport == "udp" else "-t"
    try:
        text = subprocess.run([tool, flag, "-a", "-n", "-m", "-H", "sport", "=", f":{port}"],
                              capture_output=True, text=True, timeout=2).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    total = 0
    for block in re.findall(r"skmem:\(([^)]*)\)", text):
        fields = dict((k, int(v)) for k, v in re.findall(r"([a-z]+)(\d+)", block))
        total += fields.get("r", 0) + fields.get("w", 0)
    return total / 1024


class SocketSampler:
    """Samples server_socket_kib every 100 ms between MEASURE_START and MEASURE_END.
    One reading at the end missed most of a run and the whole-machine counter in
    /proc/net/sockstat moves in coarse steps, so we keep the mean and the peak."""

    def __init__(self, port, transport):
        import threading
        self.port, self.transport, self.values = port, transport, []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)

    def loop(self):
        while not self.stop.wait(0.1):
            value = server_socket_kib(self.port, self.transport)
            if value is None:
                return
            self.values.append(value)

    def start(self):
        self.thread.start()

    def finish(self):
        self.stop.set()
        if self.thread.is_alive():
            self.thread.join()
        if not self.values:
            return "", ""
        return statistics.mean(self.values), max(self.values)


def difference(before, after):
    return {key: after[key] - before[key] for key in before}


def start_server(variant, job, folder, trace=False):
    engine = variant.split("-")[0]
    server_cpus, sq_cpu, _ = place(job["threads"], uses_sqpoll(variant), job["conns"])
    command = [str(ROOT / "bin" / f"{engine}_server"), "--port", "0", "--app", job["app"],
               "--transport", job["transport"], "--threads", str(job["threads"]),
               "--cpus", ",".join(map(str, server_cpus))]
    command += ["--buffer", str(job.get("buffer", DEFAULT_BUFFER))]
    if variant == "epoll-et":
        command += ["--epoll-mode", "et"]
    if variant == "epoll-lt-direct":
        command += ["--epoll-mode", "lt-direct"]
    if variant.endswith("-full"):
        command += ["--send-mode", "full"]
    if uses_sqpoll(variant):
        command += ["--sqpoll", "1", "--sqpoll-cpu", str(sq_cpu)]
    if variant == "io_uring-sqpoll-busy":
        command += ["--sqpoll-busy", "1"]
    depth = re.fullmatch(r"io_uring-d(\d+)", variant)
    if depth:
        command += ["--udp-depth", depth.group(1)]
    if trace:
        command = [shutil.which("strace"), "-f", "-c", "-o", str(folder / "strace.txt"), "--", *command]
    (folder / "command.json").write_text(json.dumps(command))
    out = (folder / "server.stdout").open("wb")
    err = (folder / "server.stderr").open("wb")
    process = subprocess.Popen(command, stdout=out, stderr=err)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            break
        for line in (folder / "server.stdout").read_text().splitlines():
            if line.startswith("READY "):
                # Interrupts are counted on every core the server uses, including
                # the SQPOLL thread's core, where its sends are processed.
                counted = server_cpus + ([sq_cpu] if sq_cpu is not None else [])
                return process, int(line.split()[1]), counted
        time.sleep(0.01)
    process.kill()
    raise RuntimeError("server did not start: " + (folder / "server.stderr").read_text())


def stop_server(process, folder, pid=None):
    try:
        os.kill(pid or process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    summary = None
    for line in (folder / "server.stdout").read_text().splitlines():
        if line.startswith("{"):
            summary = json.loads(line)
    return summary, process.returncode


def run_one(job, folder, duration, warmup):
    folder.mkdir(parents=True, exist_ok=True)
    variant = job["variant"]
    process, port, server_cpus = start_server(variant, job, folder)
    _, _, client_cpus = place(job["threads"], uses_sqpoll(variant), job["conns"])
    command = [str(ROOT / "bin" / "loadgen"), "--port", str(port), "--app", job["app"],
               "--transport", job["transport"], "--conns", str(job["conns"]),
               "--active", str(job["active"]), "--size", str(job["size"]),
               "--duration", str(duration), "--warmup", str(warmup), "--rate", str(job["rate"]),
               "--pace", PACE, "--threads", str(len(client_cpus)), "--cpus", ",".join(map(str, client_cpus))]
    (folder / "client_command.json").write_text(json.dumps(command))
    client = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    before = after = None
    report = None
    sampler = SocketSampler(port, job["transport"])
    timeout = time.monotonic() + duration + warmup + 30
    for line in client.stdout:
        if line.startswith("MEASURE_START"):
            before = (proc_snapshot(process.pid), softirq_snapshot(server_cpus), proc_snapshot(client.pid),
                      core_times(server_cpus))
            sampler.start()
        elif line.startswith("MEASURE_END"):
            after = (proc_snapshot(process.pid), softirq_snapshot(server_cpus), proc_snapshot(client.pid),
                     sockstat_kib(), *sampler.finish(), core_times(server_cpus))
        elif line.startswith("{"):
            report = json.loads(line)
        if time.monotonic() > timeout:
            client.kill()
            break
    client.wait()
    sampler.stop.set()
    client_error = client.stderr.read()
    summary, code = stop_server(process, folder)
    (folder / "server.json").write_text(json.dumps(summary))
    (folder / "client.json").write_text(json.dumps(report))
    return evaluate(job, report, summary, code, before, after, client.returncode, client_error, duration)


def evaluate(job, client, server, code, before, after, client_code, client_error, duration):
    row = {key: job[key] for key in
           ("suite", "variant", "app", "transport", "size", "conns", "active", "rate", "threads", "buffer", "run")}
    problems, warnings = [], []
    if client is None or server is None or before is None or after is None:
        row.update(valid=False, problems=f"missing evidence; client stderr: {client_error.strip()}", warnings="")
        return row
    if code != 0 or not server["server_ok"]:
        problems.append(f"server exit {code}")
    if client_code != 0 or not client["valid"]:
        problems.append(f"client invalid (errors={client['errors']})")
    if server["errors"] or server["rejected"]:
        problems.append(f"server errors={server['errors']} rejected={server['rejected']}")
    body = job["size"] if job["app"] in ("echo", "upload", "memory") else 0
    response = 1 if job["app"] in ("upload", "memory", "noop") else job["size"]
    if job["transport"] == "tcp":
        done = client["total_completed"]
        if server["incomplete"]:
            problems.append(f"incomplete={server['incomplete']}")
        if server["messages"] != done:
            problems.append(f"messages server={server['messages']} client={done}")
        if server["wire_bytes_read"] != done * (8 + body):
            problems.append("request byte total mismatch")
        if server["wire_bytes_written"] != done * response:
            problems.append("response byte total mismatch")
        if server["accepted"] != job["conns"]:
            problems.append(f"accepted {server['accepted']} of {job['conns']}")
    elif server["messages"] < client["total_completed"]:
        problems.append("client completed more datagrams than the server handled")

    tick = os.sysconf("SC_CLK_TCK")
    resource = difference(before[0], after[0])
    interval = resource["time"]
    client_resource = difference(before[2], after[2])
    softirq = difference(before[1], after[1])
    user, system = resource["utime"] / tick, resource["stime"] / tick
    cores = difference(before[3], after[6])
    core_pct = lambda key: 100 * cores[key] / tick / interval
    measured = client["measured"] or 1
    messages = server["messages"] or 1
    uring = variant_engine(job["variant"]) == "io_uring"
    kernel_entries = (server["sc_recv"] + server["sc_send"] + server["sc_ctl"] + server["sc_accept"]
                      + (server["sc_enter"] if uring else server["sc_wait"]))
    client_cpu = 100 * (client_resource["utime"] + client_resource["stime"]) / tick / interval
    client_threads = len(place(job["threads"], uses_sqpoll(job["variant"]), job["conns"])[2])
    client_per_thread = client_cpu / client_threads
    if job["rate"] == 0 and client_per_thread >= 85:
        warnings.append("load generator near saturation")
    if job["rate"] and client["measured"] < 0.95 * job["rate"] * duration:
        warnings.append("achieved rate below 95% of offered rate")
    if job["transport"] == "udp" and client["udp_timeouts"]:
        warnings.append(f"udp timeouts {client['udp_timeouts']}")
    row.update(
        valid=not problems, problems="; ".join(problems), warnings="; ".join(warnings),
        throughput_rps=client["throughput_rps"], payload_mib_s=client["payload_mib_s"],
        mean_us=client["mean_us"], p50_us=client["p50_us"], p90_us=client["p90_us"],
        p95_us=client["p95_us"], p99_us=client["p99_us"], p999_us=client["p999_us"],
        server_cpu_pct=100 * (user + system) / interval,
        server_user_us_per_req=user * 1e6 / measured, server_sys_us_per_req=system * 1e6 / measured,
        server_cpu_us_per_req=(user + system) * 1e6 / measured,
        syscalls_per_msg=kernel_entries / messages, recv_per_msg=server["sc_recv"] / messages,
        send_per_msg=server["sc_send"] / messages, wait_per_msg=server["sc_wait"] / messages,
        ctl_per_msg=server["sc_ctl"] / messages, enter_per_msg=server["sc_enter"] / messages,
        loop_per_msg=server["loop_calls"] / messages,
        voluntary_cs_per_msg=resource["voluntary"] / measured,
        involuntary_cs=resource["involuntary"],
        net_rx_softirq_per_msg=softirq["NET_RX"] / measured, net_tx_softirq_per_msg=softirq["NET_TX"] / measured,
        hard_irq_per_msg=softirq["HARD"] / measured, rss_kib=server["max_rss_kib"],
        irq_timer_per_msg=softirq["LOC"] / measured, irq_resched_per_msg=softirq["RES"] / measured,
        irq_call_per_msg=softirq["CAL"] / measured, irq_device_per_msg=softirq["DEV"] / measured,
        irq_other_per_msg=softirq["OTHER"] / measured, kernel_sock_mem_kib=after[3],
        server_sock_kib_mean=after[4] if len(after) > 4 else "",
        server_sock_kib_peak=after[5] if len(after) > 5 else "",
        server_cores_busy_pct=core_pct("busy"), server_cores_softirq_pct=core_pct("softirq"),
        server_cores_irq_pct=core_pct("irq"),
        server_app_runqueue_ms_per_s=(resource["runqueue"] - resource["kernel_runqueue"]) / 1e6 / interval,
        server_kernel_runqueue_ms_per_s=resource["kernel_runqueue"] / 1e6 / interval,
        client_cpu_per_thread=client_per_thread, udp_timeouts=client["udp_timeouts"],
        datagrams_dropped=server["datagrams_dropped"], server_tasks=after[0]["tasks"],
        max_window_us=max(client["window_max_us"] or [0]),
        server_runqueue_ms_per_s=resource["runqueue"] / 1e6 / interval,
        # Only meaningful for paced runs; closed-loop latency is often above 10 ms anyway.
        windows_over_10ms=sum(1 for w in client["window_max_us"] if w > 10000) if job["rate"] else "")
    return row


def uses_sqpoll(variant):
    return variant.startswith("io_uring-sqpoll")


def variant_engine(variant):
    return variant.split("-")[0]


def build_jobs(names, runs):
    jobs = []
    for name in names:
        suite = SUITES[name]
        for variant in suite.get("variants", BASE):
            for conns in suite["conns"]:
                for rate in suite.get("rates", [0]):
                    for threads in suite.get("threads", [1]):
                        for buffer in suite.get("buffers", [DEFAULT_BUFFER]):
                            for run in range(1, runs + 1):
                                offered = rate or suite.get("rate_per_conn", 0) * conns
                                jobs.append(dict(
                                    suite=name, variant=variant, app=suite["app"], size=suite["size"],
                                    transport=suite.get("transport", "tcp"), conns=conns,
                                    active=suite.get("active", conns), rate=offered, threads=threads,
                                    buffer=buffer, run=run))
    return jobs


def job_id(job):
    return "{suite}/{variant}-c{conns}-a{active}-r{rate}-t{threads}-b{buffer}-run{run}".format(**job)


FIELDS = ["suite", "variant", "app", "transport", "size", "conns", "active", "rate", "threads", "buffer", "run",
          "valid", "problems", "warnings", "throughput_rps", "payload_mib_s", "mean_us", "p50_us",
          "p90_us", "p95_us", "p99_us", "p999_us", "server_cpu_pct", "server_user_us_per_req",
          "server_sys_us_per_req", "server_cpu_us_per_req", "syscalls_per_msg", "recv_per_msg",
          "send_per_msg", "wait_per_msg", "ctl_per_msg", "enter_per_msg", "loop_per_msg",
          "voluntary_cs_per_msg", "involuntary_cs", "net_rx_softirq_per_msg", "net_tx_softirq_per_msg",
          "hard_irq_per_msg", "rss_kib", "client_cpu_per_thread", "udp_timeouts", "datagrams_dropped",
          "max_window_us", "windows_over_10ms", "server_runqueue_ms_per_s",
          "server_app_runqueue_ms_per_s", "server_kernel_runqueue_ms_per_s",
          "irq_timer_per_msg", "irq_resched_per_msg", "irq_call_per_msg", "irq_device_per_msg",
          "irq_other_per_msg", "kernel_sock_mem_kib", "server_sock_kib_mean", "server_sock_kib_peak",
          "server_cores_busy_pct", "server_cores_softirq_pct", "server_cores_irq_pct", "server_tasks"]


def cmd_run(args):
    names = args.suites or list(SUITES)
    jobs = build_jobs(names, args.runs)
    # All suites are interleaved so slow machine drift cannot favour one engine.
    random.Random(args.seed).shuffle(jobs)
    os.sched_setaffinity(0, {CONTROLLER_CPU})
    args.out.mkdir(parents=True, exist_ok=True)
    meta = dict(platform=platform.platform(), machine=platform.machine(), logical_cpus=os.cpu_count(),
                physical_cpus=physical_cpus(), seed=args.seed, runs=args.runs, duration=args.duration,
                warmup=args.warmup, suites={n: SUITES[n] for n in names}, pace=PACE,
                controller_cpu=CONTROLLER_CPU, client_threads_max=CLIENT_THREADS, avoid_cpu0=AVOID_CPU0,
                cpu_model=next((l.split(":")[1].strip() for l in Path("/proc/cpuinfo").read_text().splitlines()
                                if l.startswith("model name")), "unknown"),
                kernel=platform.release(), environment=environment(), kernel_accounting=kernel_accounting(),
                compiler=subprocess.run(["cc", "--version"], capture_output=True, text=True).stdout.splitlines()[0])
    (args.out / "metadata.json").write_text(json.dumps(meta, indent=2))
    path = args.out / "summary.csv"
    done = set()
    if path.exists():
        with path.open() as f:
            done = {job_id(r) for r in csv.DictReader(f)}
    else:
        with path.open("w", newline="") as f:
            csv.DictWriter(f, FIELDS).writeheader()
    # The first run after an idle period measured slow in the earlier study.
    # This throwaway run absorbs that cold start, and is never recorded.
    if not args.no_settle:
        settle = dict(jobs[0], suite="_settle", run=0)
        run_one(settle, args.out / "_settle", min(args.duration, 5), args.warmup)
    total = len(jobs)
    started = time.time()
    failures = 0
    for index, job in enumerate(jobs, 1):
        identifier = job_id(job)
        if identifier in done:
            continue
        try:
            row = run_one(job, args.out / identifier, args.duration, args.warmup)
        except Exception as exc:  # A failed attempt is recorded, not hidden.
            row = {key: job[key] for key in ("suite", "variant", "app", "transport", "size", "conns",
                                             "active", "rate", "threads", "buffer", "run")}
            row.update(valid=False, problems=f"{type(exc).__name__}: {exc}", warnings="")
        with path.open("a", newline="") as f:
            csv.DictWriter(f, FIELDS, restval="").writerow(row)
        failures += not row["valid"]
        remaining = (time.time() - started) / index * (total - index) / 60
        print(f"[{index}/{total}] {identifier} valid={row['valid']} "
              f"rps={row.get('throughput_rps', 0)} {row['problems']} ~{remaining:.0f} min left", flush=True)
    print(f"Finished. Invalid attempts: {failures}")
    return 1 if failures else 0


NUMERIC_KEEP = ["throughput_rps", "payload_mib_s", "p50_us", "p95_us", "p99_us", "p999_us", "server_cpu_pct",
                "server_user_us_per_req", "server_sys_us_per_req", "server_cpu_us_per_req", "syscalls_per_msg",
                "wait_per_msg", "ctl_per_msg", "enter_per_msg", "loop_per_msg", "voluntary_cs_per_msg",
                "net_rx_softirq_per_msg", "hard_irq_per_msg", "rss_kib", "client_cpu_per_thread",
                "irq_resched_per_msg", "irq_call_per_msg", "kernel_sock_mem_kib",
                "server_sock_kib_mean", "server_sock_kib_peak",
                "server_cores_busy_pct", "server_cores_softirq_pct", "server_cores_irq_pct",
                "server_app_runqueue_ms_per_s", "server_kernel_runqueue_ms_per_s"]
COLORS = {"select": "#0072B2", "poll": "#E69F00", "epoll": "#009E73", "io_uring": "#CC79A7",
          "epoll-et": "#56B4E9", "epoll-lt-direct": "#7FB800", "io_uring-sqpoll": "#D55E00",
          "io_uring-sqpoll-busy": "#8B4513", "select-full": "#5BA3D9", "poll-full": "#F2C14E",
          "epoll-full": "#66C2A5", "io_uring-d1": "#F0C9DC",
          "io_uring-d4": "#E28FB8", "io_uring-d32": "#CC79A7"}
PLOTS = [("throughput_rps", "Requests per second"), ("server_cpu_pct", "Server CPU (% of one core)"),
         ("server_cores_busy_pct", "Server core(s) busy, all work incl. softirq (%)"),
         ("server_cores_softirq_pct", "Softirq time on server core(s) (%)"),
         ("server_cpu_us_per_req", "Server CPU per request (microseconds)"),
         ("server_sys_us_per_req", "Kernel CPU per request (microseconds)"),
         ("syscalls_per_msg", "System calls per message"), ("p50_us", "p50 latency (microseconds)"),
         ("p99_us", "p99 latency (microseconds)"), ("rss_kib", "Peak server memory (KiB)"),
         ("voluntary_cs_per_msg", "Voluntary context switches per request"),
         ("net_rx_softirq_per_msg", "NET_RX softirqs per request"),
         ("irq_resched_per_msg", "Reschedule interrupts per request"),
         ("kernel_sock_mem_kib", "Kernel socket memory, whole machine (KiB)"),
         ("server_sock_kib_mean", "Data queued on server sockets, mean (KiB)"),
         ("server_sock_kib_peak", "Data queued on server sockets, peak (KiB)")]


def load_inputs(inputs):
    """Combine result folders. A suite found in a later folder replaces that whole
    suite from earlier folders, so a rerun never mixes with the runs it replaces."""
    rows, sources = [], {}
    for folder in inputs:
        with (folder / "summary.csv").open() as f:
            new = list(csv.DictReader(f))
        replaced = {r["suite"] for r in new}
        rows = [r for r in rows if r["suite"] not in replaced] + new
        for suite in replaced:
            sources[suite] = str(folder)
    return rows, sources


def cmd_analyze(args):
    rows, sources = load_inputs(args.input)
    usable = [r for r in rows if r["valid"] == "True"]
    invalid = [r for r in rows if r["valid"] != "True"]
    args.out.mkdir(parents=True, exist_ok=True)
    meta = json.loads((args.input[0] / "metadata.json").read_text())
    groups = collections.defaultdict(list)
    for r in usable:
        x = SUITES[r["suite"]]["x"]
        groups[r["suite"], r["variant"], int(r[x])].append(r)
    table = []
    if not groups:
        print("No valid runs to analyse.")
        return 1
    for (suite, variant, x), members in sorted(groups.items()):
        out = dict(suite=suite, variant=variant, x_name=SUITES[suite]["x"], x=x, runs=len(members))
        for key in NUMERIC_KEEP:
            # Columns added later are empty in older result folders.
            values = [float(m[key]) for m in members if m.get(key) not in (None, "")]
            if not values:
                out[key] = out[key + "_min"] = out[key + "_max"] = None
                continue
            out[key] = statistics.median(values)
            out[key + "_min"], out[key + "_max"] = min(values), max(values)
        table.append(out)
    with (args.out / "aggregates.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, list(table[0]))
        writer.writeheader()
        writer.writerows(table)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    figures = collections.defaultdict(list)
    for suite in sorted({t["suite"] for t in table}):
        data = [t for t in table if t["suite"] == suite]
        variants = [v for v in PLOT_ORDER if any(t["variant"] == v for t in data)]
        xs = sorted({t["x"] for t in data})
        for metric, label in PLOTS:
            if all(t[metric] is None for t in data):
                continue
            figure, axis = plt.subplots(figsize=(8, 4.5))
            width = 0.8 / len(variants)
            for i, variant in enumerate(variants):
                lookup = {t["x"]: t for t in data if t["variant"] == variant}
                present = lambda x: x in lookup and lookup[x][metric] is not None
                heights = [lookup[x][metric] if present(x) else 0 for x in xs]
                low = [h - lookup[x][metric + "_min"] if present(x) else 0 for x, h in zip(xs, heights)]
                high = [lookup[x][metric + "_max"] - h if present(x) else 0 for x, h in zip(xs, heights)]
                axis.bar([p + i * width for p in range(len(xs))], heights, width, label=variant,
                         color=COLORS[variant], yerr=[low, high], capsize=2, error_kw={"linewidth": 0.8})
            axis.set_xticks([p + width * (len(variants) - 1) / 2 for p in range(len(xs))])
            axis.set_xticklabels(map(str, xs))
            axis.set_xlabel({"conns": "Open connections", "rate": "Offered requests/s",
                             "threads": "Server threads", "buffer": "Server buffer (bytes)"}[SUITES[suite]["x"]])
            axis.set_ylabel(label)
            axis.set_title(f"{suite}: median of repetitions, whiskers show min and max")
            axis.grid(axis="y", alpha=.3)
            axis.legend(fontsize=8)
            name = f"{suite}__{metric}.png"
            figure.tight_layout()
            figure.savefig(args.out / name, dpi=150)
            plt.close(figure)
            figures[suite].append(name)

    lines = ["# Measured results", "",
             f"Platform: {meta['platform']}; CPU: {meta['cpu_model']}; {meta['logical_cpus']} logical CPUs; "
             f"compiler: {meta['compiler']}.",
             f"Each run: {meta['warmup']} s warmup then {meta['duration']} s measured; "
             f"{meta['runs']} repetitions per setting; all suites interleaved (seed {meta['seed']}).",
             f"Valid attempts: {len(usable)}; invalid attempts: {len(invalid)}.", ""]
    if len(args.input) > 1:
        lines += ["## Sources", "", "Each suite comes entirely from one run folder:", ""]
        lines += [f"- {suite}: `{source}`" for suite, source in sorted(sources.items())] + [""]
        for folder in args.input[1:]:
            other = json.loads((folder / "metadata.json").read_text())
            if (other["platform"], other["cpu_model"], other["environment"]) != (meta["platform"], meta["cpu_model"], meta["environment"]):
                lines += [f"**Warning:** `{folder}` was recorded on a different machine or with different settings.", ""]
    if invalid:
        lines += ["## Invalid attempts", ""]
        lines += [f"- {r['suite']} {r['variant']} conns={r['conns']} run={r['run']}: {r['problems']}" for r in invalid]
        lines.append("")
    warned = collections.Counter(w for r in usable for w in r["warnings"].split("; ") if w)
    if warned:
        lines += ["## Warnings", ""] + [f"- {w}: {n} runs" for w, n in warned.most_common(20)] + [""]
    for suite in sorted({t["suite"] for t in table}):
        data = [t for t in table if t["suite"] == suite]
        lines += [f"## {suite}", "", SUITES[suite]["purpose"], "",
                  f"| Variant | {data[0]['x_name']} | Runs | req/s | CPU % | CPU us/req | user us | kernel us | syscalls/msg | p50 us | p99 us | RSS KiB |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for t in data:
            lines.append(f"| {t['variant']} | {t['x']} | {t['runs']} | {t['throughput_rps']:.0f} | {t['server_cpu_pct']:.1f} | "
                         f"{t['server_cpu_us_per_req']:.2f} | {t['server_user_us_per_req']:.2f} | {t['server_sys_us_per_req']:.2f} | "
                         f"{t['syscalls_per_msg']:.3f} | {t['p50_us']:.1f} | {t['p99_us']:.1f} | {t['rss_kib']:.0f} |")
        lines += [""] + [f"![{n}]({n})" for n in figures[suite]] + [""]
    (args.out / "report.md").write_text("\n".join(lines))
    print(f"{len(usable)} valid, {len(invalid)} invalid. Report: {args.out / 'report.md'}")
    return 0


def cmd_check_counters(args):
    """Compare the servers' own syscall counters with strace on a short run."""
    args.out.mkdir(parents=True, exist_ok=True)
    outcomes = []
    for variant in ("select", "poll", "epoll", "epoll-et", "epoll-lt-direct", "select-full", "epoll-full",
                    "io_uring"):
        job = dict(suite="check", variant=variant, app="echo", size=64, transport="tcp", conns=50,
                   active=50, rate=0, threads=1, run=1)
        folder = args.out / variant
        folder.mkdir(parents=True, exist_ok=True)
        process, port, _ = start_server(variant, job, folder, trace=True)
        time.sleep(0.2)
        _, _, client_cpus = place(1, False, 50)
        subprocess.run([str(ROOT / "bin" / "loadgen"), "--port", str(port), "--conns", "50", "--size", "64",
                        "--duration", "2", "--warmup", "0.2", "--threads", "1", "--cpus", str(client_cpus[0])],
                       capture_output=True)
        children = Path(f"/proc/{process.pid}/task/{process.pid}/children").read_text().split()
        summary, _ = stop_server(process, folder, int(children[0]))
        counts = {}
        for line in (folder / "strace.txt").read_text().splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[-1].replace("_", "").isalnum() and parts[3].isdigit():
                counts[parts[-1]] = int(parts[3])
        names = dict(recv="recvfrom", send="sendto", accept="accept4")
        # ARM Linux has no poll or epoll_wait entry; libc uses ppoll and epoll_pwait.
        wait_names = dict(select=["pselect6"], poll=["poll", "ppoll"], epoll=["epoll_wait", "epoll_pwait"],
                          io_uring=["io_uring_enter"])[variant.split("-")[0]]
        observed_wait = sum(counts.get(n, 0) for n in wait_names)
        row = dict(variant=variant)
        pairs = [(summary["sc_" + k], counts.get(v, 0), k) for k, v in names.items()]
        pairs += [(summary["sc_ctl"], counts.get("epoll_ctl", 0), "ctl")]
        pairs += [(summary["sc_enter"] if variant == "io_uring" else summary["sc_wait"], observed_wait, "wait")]
        row["compared"] = [dict(name=n, counter=a, strace=b) for a, b, n in pairs]
        row["ok"] = all(abs(a - b) <= max(5, 0.02 * b) for a, b, _ in pairs)
        outcomes.append(row)
        print(json.dumps(row), flush=True)
    (args.out / "counters.json").write_text(json.dumps(outcomes, indent=2))
    return 0 if all(r["ok"] for r in outcomes) else 1




# ---- Latency-spike diagnosis ------------------------------------------------
# Samples, every 10 ms during a run: CPU used by each core's ksoftirqd thread,
# run-queue waiting time of the server and client threads, and network softirq
# counts. It then compares 100 ms windows that contain a latency spike with
# windows that do not. Sampling disturbs the run slightly, so these runs are a
# diagnosis only and never enter the results.

def ksoftirqd_pids(cpus):
    found = {}
    for task in Path("/proc").iterdir():
        if task.name.isdigit():
            name = read(task / "comm")
            for cpu in cpus:
                if name == f"ksoftirqd/{cpu}":
                    found[cpu] = int(task.name)
    return found


def task_waits(pid):
    """Total time (ns) the process's threads spent runnable but not running."""
    total = 0
    for task in Path(f"/proc/{pid}/task").iterdir():
        parts = read(task / "schedstat").split()
        if len(parts) >= 2:
            total += int(parts[1])
    return total


def cpu_ticks(pid):
    fields = read(f"/proc/{pid}/stat").rsplit(")", 1)[-1].split()
    return int(fields[11]) + int(fields[12]) if len(fields) > 12 else 0


def foreign_running(cpus, own_pids):
    """Names of other tasks that are running or runnable on these CPUs right now."""
    names = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit() or int(proc.name) in own_pids:
            continue
        try:
            tasks = list((proc / "task").iterdir())
        except OSError:
            continue
        for task in tasks:
            raw = read(task / "stat")
            if ")" not in raw:
                continue
            comm = raw[raw.index("(") + 1:raw.rindex(")")]
            fields = raw[raw.rindex(")") + 2:].split()
            if len(fields) > 36 and fields[0] == "R" and int(fields[36]) in cpus:
                names.append(comm)
    return names


def diagnose_one(variant, conns, rate, app, size, pace, duration, folder):
    import threading
    job = dict(suite="diagnose", variant=variant, app=app, size=size, transport="tcp", conns=conns,
               active=conns, rate=rate, threads=1, run=1)
    folder.mkdir(parents=True, exist_ok=True)
    process, port, server_cpus = start_server(variant, job, folder)
    _, _, client_cpus = place(1, uses_sqpoll(variant), conns)
    soft = ksoftirqd_pids(server_cpus + client_cpus)
    command = [str(ROOT / "bin" / "loadgen"), "--port", str(port), "--app", app, "--conns", str(conns),
               "--size", str(size), "--duration", str(duration), "--warmup", "1", "--rate", str(rate),
               "--pace", pace, "--threads", str(len(client_cpus)), "--cpus", ",".join(map(str, client_cpus))]
    client = subprocess.Popen(command, stdout=subprocess.PIPE, text=True)
    samples, started, stop = [], [None], threading.Event()

    def sampler():
        while not stop.is_set():
            try:
                samples.append(dict(
                    t=time.perf_counter(),
                    softirqd_client=sum(cpu_ticks(soft[c]) for c in client_cpus if c in soft),
                    softirqd_server=sum(cpu_ticks(soft[c]) for c in server_cpus if c in soft),
                    client_wait=task_waits(client.pid), server_wait=task_waits(process.pid),
                    net_rx=softirq_snapshot(client_cpus + server_cpus)["NET_RX"],
                    others=foreign_running(set(server_cpus + client_cpus),
                                           {process.pid, client.pid, os.getpid()})))
            except (OSError, ValueError):
                pass
            time.sleep(0.01)

    thread = threading.Thread(target=sampler, daemon=True)
    thread.start()
    report = None
    for line in client.stdout:
        if line.startswith("MEASURE_START"):
            started[0] = time.perf_counter()
        elif line.startswith("{"):
            report = json.loads(line)
    client.wait()
    stop.set()
    thread.join()
    stop_server(process, folder)
    windows = report["window_max_us"]
    tick_ms = 1000 / os.sysconf("SC_CLK_TCK")
    rows = []
    for index, worst in enumerate(windows):
        lo, hi = started[0] + index * 0.1, started[0] + (index + 1) * 0.1
        inside = [x for x in samples if lo <= x["t"] <= hi]
        if len(inside) < 2:
            continue
        a, b = inside[0], inside[-1]
        rows.append(dict(worst_us=worst,
                         softirqd_client_ms=(b["softirqd_client"] - a["softirqd_client"]) * tick_ms,
                         softirqd_server_ms=(b["softirqd_server"] - a["softirqd_server"]) * tick_ms,
                         client_runqueue_ms=(b["client_wait"] - a["client_wait"]) / 1e6,
                         server_runqueue_ms=(b["server_wait"] - a["server_wait"]) / 1e6,
                         net_rx=b["net_rx"] - a["net_rx"],
                         others=dict(collections.Counter(n for x in inside for n in x["others"])),
                         samples=len(inside)))
    (folder / "windows.json").write_text(json.dumps(dict(report={k: report[k] for k in report if k != "histogram"},
                                                         ksoftirqd=soft, windows=rows), indent=1))
    return report, rows


def cmd_diagnose(args):
    cases = [("epoll", 100, 40000, "echo", 64), ("io_uring", 100, 40000, "echo", 64),
             ("select", 100, 40000, "echo", 64), ("epoll", 100, 3000, "stream", 64 * KIB),
             # Plain SQPOLL had a 1-2 ms p99 at 20k req/s that followed run-queue wait.
             ("io_uring-sqpoll", 100, 20000, "echo", 64)]
    if args.variants:
        cases = [c for c in cases if c[0] in args.variants]
    paces = args.paces or ("spin", "sleep", "hybrid")
    args.out.mkdir(parents=True, exist_ok=True)
    summary = []
    os.sched_setaffinity(0, {CONTROLLER_CPU})
    for pace in paces:
        for variant, conns, rate, app, size in cases:
            for run in range(1, args.runs + 1):
                name = f"{pace}-{variant}-{app}-{rate}-run{run}"
                report, rows = diagnose_one(variant, conns, rate, app, size, pace, args.duration, args.out / name)
                spikes = [r for r in rows if r["worst_us"] > 5000]
                calm = [r for r in rows if r["worst_us"] <= 5000]
                mean = lambda group, key: statistics.mean(r[key] for r in group) if group else 0
                line = dict(pace=pace, variant=variant, app=app, rate=rate, run=run, p50_us=report["p50_us"],
                            p99_us=report["p99_us"], spike_windows=len(spikes), windows=len(rows))
                for key in ("softirqd_client_ms", "softirqd_server_ms", "client_runqueue_ms", "server_runqueue_ms"):
                    line[key + "_spike"] = round(mean(spikes, key), 3)
                    line[key + "_calm"] = round(mean(calm, key), 3)
                # Which other programs were running on our cores, as a share of samples.
                for label, group in (("spike", spikes), ("calm", calm)):
                    seen = collections.Counter()
                    for r in group:
                        seen.update(r["others"])
                    total = sum(r["samples"] for r in group) or 1
                    line["interferers_" + label] = {n: round(c / total, 3) for n, c in seen.most_common(5)}
                summary.append(line)
                print(json.dumps(line), flush=True)
    (args.out / "diagnosis.json").write_text(json.dumps(summary, indent=1))
    print("Per 100 ms window: *_spike averages windows whose worst latency exceeded 5 ms, *_calm the rest.")
    return 0


# ---- Cross-check of the load generator ------------------------------------
# The brief suggests tools such as tcpkali or wrk. wrk only speaks HTTP, and the
# original tcpkali repositories no longer exist, so instead the same offered load
# is sent by two independent clients: our C load generator and the Python client
# from the previous version (different language, different code). Server-side
# cost per request is a property of the server, so it should not depend on which
# client produced the traffic.

def crosscheck_one(engine, client_kind, rate, conns, duration, folder):
    import asyncio
    sys.path.insert(0, str(Path(__file__).parent))
    import testing_v4_reference as reference
    job = dict(suite="crosscheck", variant=engine, app="echo", size=64, transport="tcp", conns=conns,
               active=conns, rate=rate, threads=1, run=1)
    folder.mkdir(parents=True, exist_ok=True)
    process, port, server_cpus = start_server(engine, job, folder)
    _, _, client_cpus = place(1, False, conns)
    marks = {}
    if client_kind == "loadgen":
        command = [str(ROOT / "bin" / "loadgen"), "--port", str(port), "--conns", str(conns), "--size", "64",
                   "--duration", str(duration), "--warmup", "1", "--rate", str(rate), "--pace", PACE,
                   "--threads", "1", "--cpus", str(client_cpus[0])]
        client = subprocess.Popen(command, stdout=subprocess.PIPE, text=True)
        for line in client.stdout:
            if line.startswith("MEASURE_START"):
                marks["before"] = proc_snapshot(process.pid)
            elif line.startswith("MEASURE_END"):
                marks["after"] = proc_snapshot(process.pid)
            elif line.startswith("{"):
                report = json.loads(line)
        client.wait()
        completed, p50 = report["measured"], report["p50_us"]
    else:
        async def ready():
            marks["before"] = proc_snapshot(process.pid)

        def done():
            marks["after"] = proc_snapshot(process.pid)

        os.sched_setaffinity(0, {client_cpus[0]})
        try:
            report = asyncio.run(reference.run_load("127.0.0.1", port, connections=conns, requests=1, size=64,
                                                    warmup=10, duration=duration, rate=rate,
                                                    ready_hook=ready, done_hook=done))
        finally:
            os.sched_setaffinity(0, {CONTROLLER_CPU})
        completed, p50 = report["completed"], report["p50_us"]
    summary, _ = stop_server(process, folder)
    used = difference(marks["before"], marks["after"])
    tick = os.sysconf("SC_CLK_TCK")
    uring = engine == "io_uring"
    calls = (summary["sc_recv"] + summary["sc_send"] + summary["sc_ctl"] + summary["sc_accept"]
             + (summary["sc_enter"] if uring else summary["sc_wait"]))
    readiness_io = summary["sc_recv"] + summary["sc_send"] + summary["sc_ctl"]
    return dict(engine=engine, client=client_kind, achieved_rps=round(completed / used["time"], 1),
                io_calls_per_msg=round(readiness_io / summary["messages"], 3) if not uring else None,
                server_cpu_us_per_req=round((used["utime"] + used["stime"]) / tick * 1e6 / completed, 2),
                syscalls_per_msg=round(calls / summary["messages"], 3),
                wakeups_per_req=round(used["voluntary"] / completed, 3), p50_us=round(p50, 1))


def cmd_crosscheck(args):
    args.out.mkdir(parents=True, exist_ok=True)
    os.sched_setaffinity(0, {CONTROLLER_CPU})
    rows = []
    for engine in BASE:
        for run in range(1, args.runs + 1):
            for kind in ("loadgen", "python"):
                row = crosscheck_one(engine, kind, args.rate, args.conns, args.duration,
                                     args.out / f"{engine}-{kind}-run{run}")
                row["run"] = run
                rows.append(row)
                print(json.dumps(row), flush=True)
    verdicts = []
    for engine in BASE:
        pick = lambda kind, key: statistics.median(r[key] for r in rows if r["engine"] == engine and r["client"] == kind)
        keys = ["achieved_rps", "wakeups_per_req", "server_cpu_us_per_req", "syscalls_per_msg"]
        if engine != "io_uring":
            keys.insert(1, "io_calls_per_msg")
        ratio = {key: pick("python", key) / pick("loadgen", key) for key in keys if pick("loadgen", key)}
        verdicts.append(dict(engine=engine, python_over_loadgen={k: round(v, 3) for k, v in ratio.items()}))
        print(json.dumps(verdicts[-1]), flush=True)
    (args.out / "crosscheck.json").write_text(json.dumps(dict(rows=rows, ratios=verdicts), indent=1))
    print("Expected: achieved_rps and io_calls_per_msg (recv/send/epoll_ctl per message) near 1.0, because they")
    print("depend only on the server and the offered rate. CPU and wait calls per request follow wakeups_per_req:")
    print("a burstier client lets the server serve several requests per wakeup. Evenly paced arrivals give about")
    print("one wakeup per request.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")
    run = sub.add_parser("run")
    run.add_argument("--out", type=Path, required=True)
    run.add_argument("--suites", nargs="+", choices=list(SUITES))
    run.add_argument("--runs", type=int, default=5)
    run.add_argument("--duration", type=float, default=10)
    run.add_argument("--warmup", type=float, default=2)
    run.add_argument("--seed", type=int, default=2026)
    run.add_argument("--no-settle", action="store_true")
    analyze = sub.add_parser("analyze")
    analyze.add_argument("--input", type=Path, nargs="+", required=True,
                         help="One or more result folders; later folders replace whole suites")
    analyze.add_argument("--out", type=Path, required=True)
    diagnose = sub.add_parser("diagnose")
    diagnose.add_argument("--out", type=Path, required=True)
    diagnose.add_argument("--runs", type=int, default=3)
    diagnose.add_argument("--duration", type=float, default=10)
    diagnose.add_argument("--variants", nargs="+", help="Only diagnose these variants")
    diagnose.add_argument("--paces", nargs="+", choices=["spin", "sleep", "hybrid"])
    cross = sub.add_parser("crosscheck")
    cross.add_argument("--out", type=Path, required=True)
    cross.add_argument("--runs", type=int, default=3)
    cross.add_argument("--rate", type=float, default=2000)
    cross.add_argument("--conns", type=int, default=10)
    cross.add_argument("--duration", type=float, default=10)
    check = sub.add_parser("check-counters")
    check.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "list":
        total = 0
        for name, suite in SUITES.items():
            count = len(build_jobs([name], 5))
            total += count
            print(f"{name:24s} {count:4d} runs  {suite['purpose']}")
        print(f"Total: {total} runs, about {total * 13 / 3600:.1f} hours at 12 s per run")
        return 0
    return {"run": cmd_run, "analyze": cmd_analyze, "check-counters": cmd_check_counters,
            "diagnose": cmd_diagnose, "crosscheck": cmd_crosscheck}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
