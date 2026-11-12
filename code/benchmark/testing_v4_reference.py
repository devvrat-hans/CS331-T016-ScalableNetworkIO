"""Standalone project tool: client, correctness tests, and benchmark.

Usage: python3 chirag/testing.py {build|client|test|benchmark|profile|analyze|capacity|capacity-analyze|selftest} [options]
Standard library for execution; matplotlib for plots, strace for profiling.
No other project Python module is imported.
"""
import argparse
import asyncio
import json
import math
from pathlib import Path
import socket
import struct
import subprocess
import time
import tempfile
import csv
import hashlib
import os
import platform
import random
import sys
import signal
import re
import shlex
import shutil
import statistics
import multiprocessing

ROOT = Path(__file__).resolve().parent  # code/benchmark: bin/ and build/ live here
ENGINES = ("select", "poll", "epoll", "io_uring")
APPS = ("echo", "upload", "download")
MAX_TRANSFER = 64 * 1024 * 1024
EXTRA_ARGS = []  # Set by correctness.py to test epoll ET, SQPOLL and threads.


# ----- Traffic generation and safe server-process ownership -----


def payload(size):
    """Byte at position i is i % 251, matching the server's test pattern."""
    return (bytes(range(251)) * ((size + 250) // 251))[:size]


class RunningServer:
    """Own exactly one subprocess; never kill processes by name or port."""
    def __init__(self, engine, app, folder, max_clients=10000, profile=False, cpu=None):
        if engine not in ENGINES or app not in APPS:
            raise ValueError("Unknown engine/application")
        self.folder = Path(folder)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.stdout_path = self.folder / "server.stdout"
        self.stderr_path = self.folder / "server.stderr"
        self.process = None
        self.summary = None
        self.returncode = None
        self.command = [str(ROOT / "bin" / f"{engine}_server"), "--port", "0",
                        "--app", app, "--max-clients", str(max_clients), *EXTRA_ARGS]
        self.profile = profile
        self.pid = None
        self.launched_at = time.perf_counter()
        if profile:
            tracer = shutil.which("strace")
            if not tracer:
                raise RuntimeError("strace is required for profile; ordinary benchmark needs no tracer")
            self.command = [tracer, "-f", "-c", "-o", str(self.folder / "syscalls.txt"), "--", *self.command]
        (self.folder / "command.json").write_text(json.dumps(self.command, indent=2))
        with self.stdout_path.open("wb") as out, self.stderr_path.open("wb") as err:
            self.process = subprocess.Popen(self.command, stdout=out, stderr=err,
                                            env={**os.environ, "LC_ALL": "C"})
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                break
            for line in self.stdout_path.read_text().splitlines():
                if line.startswith("READY "):
                    self.port = int(line.split()[1])
                    self.pid = self.child_pid()
                    if self.pid is None:
                        self.stop()
                        raise RuntimeError("Could not identify the owned server process")
                    if cpu is not None:
                        try:
                            os.sched_setaffinity(self.pid, {cpu})
                        except OSError:
                            self.stop()
                            raise
                    return
            time.sleep(0.01)
        self.stop()
        raise RuntimeError(f"{engine} did not start: {self.stderr_path.read_text().strip()}")

    def child_pid(self):
        if not self.profile:
            return self.process.pid
        children = Path(f"/proc/{self.process.pid}/task/{self.process.pid}/children")
        try:
            pids = children.read_text().split()
            return int(pids[0]) if len(pids) == 1 else None
        except FileNotFoundError:
            return None

    def stop(self):
        if self.process is None:
            return None
        if self.process.poll() is None:
            target = self.pid or self.child_pid()
            try:
                if target is not None:
                    os.kill(target, signal.SIGTERM)
                else:
                    self.process.terminate()
            except ProcessLookupError:
                pass
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    if target is not None:
                        os.kill(target, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
        self.returncode = self.process.returncode
        if not hasattr(self, "lifetime_wall_s"):
            self.lifetime_wall_s = time.perf_counter() - self.launched_at
        for line in self.stdout_path.read_text().splitlines():
            if line.startswith("{"):
                self.summary = json.loads(line)
        return self.summary

    def connect(self):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return sock


def recv_exact(sock, length):
    chunks = []
    while length:
        data = sock.recv(length)
        if not data:
            raise RuntimeError("Unexpected EOF before complete response")
        chunks.append(data)
        length -= len(data)
    return b"".join(chunks)


async def request(reader, writer, app, data):
    writer.write(struct.pack("!Q", len(data)))
    if app != "download":
        for offset in range(0, len(data), 65536):
            writer.write(data[offset:offset + 65536])
            await writer.drain()
    else:
        await writer.drain()
    if app == "upload":
        if await reader.readexactly(1) != b"K":
            raise ValueError("Upload was not acknowledged")
    else:
        for offset in range(0, len(data), 65536):
            expected = data[offset:offset + 65536]
            actual = await reader.readexactly(len(expected))
            if actual != expected:
                raise ValueError("Response content mismatch")


async def run_load(host, port, app="echo", connections=10, active=None,
                   requests=100, size=64, warmup=10, timeout=10, duration=0, server_pid=None, ready_hook=None, done_hook=None, rate=0):
    active = connections if active is None else active
    if (app not in APPS or connections < 1 or not 1 <= active <= connections or
            requests < 1 or warmup < 0 or timeout <= 0 or duration < 0 or not math.isfinite(duration) or not math.isfinite(rate) or rate < 0 or not 1 <= size <= MAX_TRANSFER or
            (app == "echo" and size > 4096)):
        raise ValueError("Invalid workload settings (echo maximum: 4096 bytes)")
    data = payload(size)
    streams = []
    errors = []
    samples = []
    attempted = 0
    start = None
    elapsed = 0.0
    finished_at = None
    deadline = 0.0
    per_connection = [0] * active
    client_cpu_start = client_cpu_s = 0.0
    resource_before = resource_after = None

    async def open_one():
        reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout)
        writer.get_extra_info("socket").setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        streams.append((reader, writer))

    async def worker(index, count, measured):
        nonlocal attempted
        reader, writer = streams[index]
        finished = 0
        next_request = time.perf_counter() + (index / rate if measured and rate else 0)
        interval = active / rate if measured and rate else 0
        while finished < count or (measured and time.perf_counter() < deadline):
            if interval:
                await asyncio.sleep(max(0, next_request - time.perf_counter()))
                if finished >= count and time.perf_counter() >= deadline:
                    break
                next_request += interval
                now = time.perf_counter()
                if next_request <= now:
                    next_request = now + interval
            if measured:
                attempted += 1
            before = time.perf_counter_ns()
            try:
                await asyncio.wait_for(request(reader, writer, app, data), timeout)
            except (OSError, ValueError, asyncio.IncompleteReadError, asyncio.TimeoutError) as exc:
                errors.append(f"connection {index}: {type(exc).__name__}: {exc}")
                break  # A failed stream cannot safely continue the protocol.
            finished += 1
            if measured:
                per_connection[index] += 1
                samples.append((time.perf_counter_ns() - before) / 1000)

    try:
        opened = await asyncio.gather(*(open_one() for _ in range(connections)), return_exceptions=True)
        errors.extend(f"connect: {type(e).__name__}: {e}" for e in opened if isinstance(e, BaseException))
        if not errors:
            await asyncio.gather(*(worker(i, warmup, False) for i in range(active)))
        if not errors:
            if ready_hook is not None:
                await ready_hook()
            resource_before = process_snapshot(server_pid) if server_pid is not None else None
            client_cpu_start = time.process_time()
            start = time.perf_counter()
            deadline = start + duration
            await asyncio.gather(*(worker(i, requests, True) for i in range(active)))
            finished_at = time.perf_counter()
            elapsed = finished_at - start
            client_cpu_s = time.process_time() - client_cpu_start
            resource_after = process_snapshot(server_pid) if server_pid is not None else None
            if done_hook is not None:
                done_hook()
    finally:
        for _, writer in streams:
            writer.close()
        await asyncio.gather(*(writer.wait_closed() for _, writer in streams), return_exceptions=True)

    ordered = sorted(samples)
    def percentile(p):
        return ordered[max(0, math.ceil(p * len(ordered)) - 1)] if ordered else None
    valid = not errors and len(samples) == attempted and all(n >= requests for n in per_connection)
    resources = resource_difference(resource_before, resource_after) if resource_before else {}
    return {
        "valid": valid, "app": app, "connections": connections, "active": active,
        "requests_per_active_connection": requests, "warmup_per_active_connection": warmup,
        "payload_bytes": size, "attempted": attempted, "completed": len(samples),
        "elapsed_s": elapsed, "throughput_requests_s": len(samples) / elapsed if elapsed else 0,
        "payload_mib_s": len(samples) * size / 1048576 / elapsed if elapsed else 0,
        "p50_us": percentile(.5), "p95_us": percentile(.95), "p99_us": percentile(.99),
        "errors": errors, "latencies_us": samples, "duration_requested_s": duration,
        "measured_per_connection": per_connection, "client_measured_cpu_s": client_cpu_s,
        "client_cpu_percent": 100 * client_cpu_s / elapsed if elapsed else 0,
        "server_measurement": resources,
        "measurement_started_s": start, "measurement_finished_s": finished_at,
        "rate_requested_s": rate, "client_cpu_affinity": sorted(os.sched_getaffinity(0)),
    }


def client_main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9090)
    parser.add_argument("--app", choices=APPS, default="echo")
    parser.add_argument("--connections", type=int, default=10)
    parser.add_argument("--active", type=int)
    parser.add_argument("--requests", type=int, default=100)
    parser.add_argument("--bytes", type=int, default=64, dest="size")
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--duration", type=float, default=0, help="Minimum measured seconds; also completes --requests per active connection")
    args = parser.parse_args(argv)
    result = asyncio.run(run_load(**vars(args)))
    print(json.dumps(result))
    return 0 if result["valid"] else 1


# ----- Correctness tests -----


def healthy_summary(server):
    summary = server.stop()
    assert server.returncode == 0 and summary and summary["server_ok"], "Server failed"
    assert summary["errors"] == summary["rejected"] == summary["incomplete"] == 0, summary
    return summary


def test_echo(engine, folder):
    s = RunningServer(engine, "echo", folder)
    try:
        with s.connect() as sock:
            # Fragment both header and payload; then pipeline several frames.
            data = payload(4096)
            frame = struct.pack("!Q", len(data)) + data
            for start in range(0, len(frame), 7):
                sock.sendall(frame[start:start + 7])
            assert recv_exact(sock, len(data)) == data
            frames = [payload(n) for n in (1, 64, 4096, 127)]
            sock.sendall(b"".join(struct.pack("!Q", len(p)) + p for p in frames))
            sock.shutdown(socket.SHUT_WR)
            for p in frames:
                assert recv_exact(sock, len(p)) == p
            assert sock.recv(1) == b"", "Expected EOF after complete echoes"
        # Real concurrent requests plus idle sockets, followed by connection churn.
        result = asyncio.run(run_load("127.0.0.1", s.port, connections=20, active=12,
                                     requests=10, warmup=2, size=64))
        assert result["valid"], result["errors"]
        for _ in range(30):
            with s.connect() as sock:
                sock.sendall(struct.pack("!Q", 1) + b"x")
                assert recv_exact(sock, 1) == b"x"
        summary = healthy_summary(s)
        assert summary["messages"] == 5 + 12 * 12 + 30, summary
    finally:
        s.stop()


def test_bulk(engine, app, folder):
    s = RunningServer(engine, app, folder)
    try:
        size = 4 * 1024 * 1024 + 17  # Crosses many 4 KiB boundaries.
        data = payload(size)
        with s.connect() as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            sock.sendall(struct.pack("!Q", size))
            if app == "upload":
                sock.sendall(data)
                sock.shutdown(socket.SHUT_WR)
                assert recv_exact(sock, 1) == b"K"
            else:
                sock.shutdown(socket.SHUT_WR)
                time.sleep(.05)  # Deliberately let the server hit backpressure.
                assert recv_exact(sock, size) == data
            assert sock.recv(1) == b""
        result = asyncio.run(run_load("127.0.0.1", s.port, app=app, connections=4,
                                     requests=3, warmup=1, size=65537))
        assert result["valid"], result["errors"]
        summary = healthy_summary(s)
        assert summary["messages"] == 17, summary
    finally:
        s.stop()


def test_errors_and_shutdown(engine, folder):
    s = RunningServer(engine, "echo", folder)
    sockets = []
    try:
        for frame in (struct.pack("!Q", 0), struct.pack("!Q", 5) + b"ab"):
            with s.connect() as sock:
                sock.sendall(frame)
                sock.shutdown(socket.SHUT_WR)
                try:
                    assert sock.recv(1) == b"", "Malformed request was accepted"
                except ConnectionResetError:
                    pass
        # A bad client must not kill the whole server.
        with s.connect() as sock:
            sock.sendall(struct.pack("!Q", 2) + b"OK")
            assert recv_exact(sock, 2) == b"OK"
        # Shutdown while receive operations are pending (particularly io_uring).
        sockets = [s.connect() for _ in range(12)]
        time.sleep(.03)
        summary = s.stop()
        assert s.returncode == 0 and summary["server_ok"], summary
        assert summary["errors"] == 2 and summary["incomplete"] == 2, summary
        assert summary["messages"] == 1, summary
    finally:
        s.stop()
        for sock in sockets:
            sock.close()


def test_limit(engine, folder):
    s = RunningServer(engine, "echo", folder, max_clients=1)
    try:
        with s.connect() as first:
            first.sendall(struct.pack("!Q", 1) + b"x")
            assert recv_exact(first, 1) == b"x"
            with s.connect() as second:
                second.sendall(struct.pack("!Q", 1) + b"y")
                try:
                    assert second.recv(1) == b""
                except ConnectionResetError:
                    pass
        summary = s.stop()
        assert s.returncode == 0 and summary["rejected"] == 1, summary
    finally:
        s.stop()


def test_many_connections(engine, folder):
    s = RunningServer(engine, "echo", folder)
    sockets = []
    try:
        result = asyncio.run(run_load("127.0.0.1", s.port, connections=300,
                                     requests=3, warmup=1, size=64))
        assert result["valid"], result["errors"]
        # Exceed io_uring SQ depth during cancellation; remain below select's ceiling.
        sockets = [s.connect() for _ in range(300)]
        time.sleep(.1)
        summary = healthy_summary(s)
        assert summary["messages"] == 1200, summary
    finally:
        s.stop()
        for sock in sockets:
            sock.close()


def test_invalid_requests(engine, folder):
    """Reject invalid lengths and corrupt upload content without killing the server."""
    cases = [("echo", 4097), ("upload", MAX_TRANSFER + 1),
             ("download", (1 << 64) - 1)]
    for index, (app, length) in enumerate(cases):
        server = RunningServer(engine, app, folder / f"length-{index}")
        try:
            with server.connect() as sock:
                sock.sendall(struct.pack("!Q", length))
                sock.shutdown(socket.SHUT_WR)
                try:
                    assert sock.recv(1) == b"", "Invalid length was accepted"
                except ConnectionResetError:
                    pass
            good = asyncio.run(run_load("127.0.0.1", server.port, app=app,
                                        connections=1, requests=1, warmup=0, size=64))
            assert good["valid"], good
            summary = server.stop()
            assert server.returncode == 0 and summary["server_ok"], summary
            assert summary["errors"] == summary["incomplete"] == 1, summary
            assert summary["messages"] == 1, summary
        finally:
            server.stop()

    server = RunningServer(engine, "upload", folder / "corrupt-upload")
    try:
        data = bytearray(payload(4097))
        data[-1] ^= 0xff  # Corrupt the first byte after the 4 KiB boundary.
        with server.connect() as sock:
            sock.sendall(struct.pack("!Q", len(data)) + data)
            sock.shutdown(socket.SHUT_WR)
            try:
                assert sock.recv(1) == b"", "Corrupt upload received an ACK"
            except ConnectionResetError:
                pass
        good = asyncio.run(run_load("127.0.0.1", server.port, app="upload",
                                    connections=1, requests=1, warmup=0, size=4097))
        assert good["valid"], good
        summary = server.stop()
        assert server.returncode == 0 and summary["server_ok"], summary
        assert summary["errors"] == summary["incomplete"] == 1, summary
        assert summary["messages"] == 1, summary
    finally:
        server.stop()


def test_reset_during_send(engine, folder):
    """An abruptly reset download connection must not terminate the server."""
    server = RunningServer(engine, "download", folder)
    try:
        with server.connect() as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            sock.sendall(struct.pack("!Q", MAX_TRANSFER))
            assert recv_exact(sock, 1) == payload(1)
            # Leave most of the response unread; closing with this linger setting resets TCP.
        result = asyncio.run(run_load("127.0.0.1", server.port, app="download",
                                      connections=2, requests=2, warmup=0, size=8193))
        assert result["valid"], result
        summary = server.stop()
        assert server.returncode == 0 and summary["server_ok"], summary
        assert summary["messages"] >= 4, summary
        # The exact reset/error accounting depends on when the kernel observes it.
    finally:
        server.stop()


def test_signal_and_options(engine, folder):
    """Check idle SIGINT and reject bad startup settings before announcing READY."""
    server = RunningServer(engine, "echo", folder / "sigint")
    try:
        server.process.send_signal(signal.SIGINT)
        server.process.wait(timeout=5)
        summary = healthy_summary(server)
        assert summary["messages"] == 0, summary
    finally:
        server.stop()
    binary = ROOT / "bin" / f"{engine}_server"
    bad_options = [("--port", "-1"), ("--port", "65536"), ("--port", "12x"),
                   ("--max-clients", "0"), ("--app", "unknown"),
                   ("--bind", "not-an-address"), ("--unknown", "1"), ("--port",),
                   ("--threads", "0"), ("--transport", "sctp"), ("--udp-depth", "0"),
                   ("--udp-depth", "257"), ("--epoll-mode", "xx"), ("--buffer", "1024"),
                   ("--buffer", "2097152"), ("--sqpoll-busy", "1"), ("--send-mode", "xx")]
    outcomes = []
    for options in bad_options:
        result = subprocess.run([str(binary), *options], capture_output=True,
                                text=True, timeout=5)
        outcomes.append({"options": options, "exit_code": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        assert result.returncode == 2, outcomes[-1]
        assert "READY " not in result.stdout, outcomes[-1]
    (folder / "invalid-options.json").write_text(json.dumps(outcomes, indent=2))


def test_main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engines", nargs="+", choices=ENGINES, default=list(ENGINES))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    folder = args.out or Path(tempfile.mkdtemp(prefix="pro-v4-tests-"))
    outcomes = []
    for engine in args.engines:
        for name, function in (("echo", test_echo),
                               ("upload", lambda e, p: test_bulk(e, "upload", p)),
                               ("download", lambda e, p: test_bulk(e, "download", p)),
                               ("errors_shutdown", test_errors_and_shutdown),
                               ("limit", test_limit),
                               ("many_connections", test_many_connections),
                               ("invalid_requests", test_invalid_requests),
                               ("reset_during_send", test_reset_during_send),
                               ("signal_options", test_signal_and_options)):
            try:
                function(engine, folder / engine / name)
                outcome = {"engine": engine, "test": name, "passed": True}
            except Exception as exc:
                outcome = {"engine": engine, "test": name, "passed": False,
                           "error": f"{type(exc).__name__}: {exc}"}
            outcomes.append(outcome)
            print(json.dumps(outcome), flush=True)
    (folder / "tests.json").write_text(json.dumps(outcomes, indent=2))
    print(f"Logs and results: {folder}")
    return 0 if all(row["passed"] for row in outcomes) else 1


# ----- Build provenance and common measurement helpers -----


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_hashes():
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(ROOT.rglob("*"))
            if p.suffix in (".c", ".h", ".py") and "results" not in p.parts}


def check_consistency():
    """These helpers must remain equal even though the four programs are standalone."""
    # Legacy v4 check (standalone servers); the v5 servers share code/benchmark/common.
    code = ROOT.parent
    paths = {"select": code / "select/select_server.c", "poll": code / "poll/poll_server.c",
             "epoll": code / "epoll/epoll_server.c", "io_uring": code / "io_uring/io_uring_server.c"}
    texts = [path.read_text() for path in paths.values()]
    names = ("handle_signal", "next_header", "client_add", "client_close", "client_is_reading",
             "client_buffer", "client_length", "prepare_chunk", "client_complete", "number",
             "seconds", "peak_rss_kib", "main")
    for name in names:
        bodies = [re.search(r"^[^\n]*\b" + name + r"\([^;]*?\n\{.*?^\}", text, re.M | re.S)
                  for text in texts]
        if any(match is None for match in bodies) or len({match.group() for match in bodies}) != 1:
            raise ValueError(f"Standalone helper mismatch: {name}; coordinate changes across owners")
    for name in ("BUFFER_SIZE", "MAX_TRANSFER", "WAIT_MS", "DEFAULT_MAX_CLIENTS", "LISTEN_BACKLOG"):
        values = [re.search(r"^#define " + name + r" (.+)$", text, re.M) for text in texts]
        if any(match is None for match in values) or len({match.group(1) for match in values}) != 1:
            raise ValueError(f"Standalone constant mismatch: {name}")
    return paths


def build_main(argv):
    parser = argparse.ArgumentParser(description="Build standalone servers and record exact commands/hashes")
    parser.add_argument("--engines", nargs="+", choices=ENGINES, default=list(ENGINES))
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cflags", default="-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror")
    parser.add_argument("--ldflags", default="")
    parser.add_argument("--uring-cflags", default=os.environ.get("URING_CFLAGS"))
    parser.add_argument("--uring-libs", default=os.environ.get("URING_LIBS"))
    args = parser.parse_args(argv)
    paths = check_consistency()
    compiler = shlex.split(args.cc)
    if not compiler:
        parser.error("Compiler command cannot be empty")
    version = subprocess.run([*compiler, "--version"], capture_output=True, text=True, check=True).stdout
    (ROOT / "bin").mkdir(exist_ok=True)
    for engine in dict.fromkeys(args.engines):
        output = ROOT / "bin" / f"{engine}_server"
        temporary = output.with_suffix(".building")
        flags = shlex.split(args.cflags)
        libraries = shlex.split(args.ldflags)
        if engine == "io_uring":
            for option, override, fallback, destination in (
                    ("--cflags", args.uring_cflags, "", flags),
                    ("--libs", args.uring_libs, "-luring", libraries)):
                if override is None:
                    tool = shutil.which("pkg-config")
                    result = subprocess.run([tool, option, "liburing"], capture_output=True, text=True) if tool else None
                    override = result.stdout.strip() if result and result.returncode == 0 else fallback
                destination.extend(shlex.split(override))
        command = [*compiler, *flags, str(paths[engine]), *libraries, "-o", str(temporary)]
        try:
            subprocess.run(command, check=True)
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
        linked_files = {str(Path(arg).resolve()): digest(arg) for arg in libraries
                        if Path(arg).is_file()}
        write_json(ROOT / "bin" / f"{engine}.build.json", {
            "engine": engine, "command": command, "compiler_version": version,
            "source": str(paths[engine].relative_to(ROOT)), "source_sha256": digest(paths[engine]),
            "binary_sha256": digest(output), "linked_file_sha256": linked_files,
            "built_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        })
        print(f"Built {engine}; exact build command saved", flush=True)
    return 0


def process_snapshot(pid):
    """Read the owned server's CPU ticks and scheduler counters; never infer them from loop_calls."""
    before = time.perf_counter()
    stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    status = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, value = line.partition(":")
        status[key] = value.strip()
    after = time.perf_counter()
    return {"time": (before + after) / 2, "cpu_ticks": int(stat[11]) + int(stat[12]),
            "voluntary": int(status["voluntary_ctxt_switches"]),
            "involuntary": int(status["nonvoluntary_ctxt_switches"]),
            "threads": int(status["Threads"]), "snapshot_span_s": after - before,
            "affinity": sorted(os.sched_getaffinity(pid))}


def resource_difference(before, after):
    elapsed = after["time"] - before["time"]
    ticks = after["cpu_ticks"] - before["cpu_ticks"]
    cpu = ticks / os.sysconf("SC_CLK_TCK")
    if elapsed <= 0 or ticks < 0:
        raise ValueError("Invalid process-resource measurement interval")
    return {"interval_s": elapsed, "cpu_s": cpu, "cpu_percent": 100 * cpu / elapsed,
            "cpu_tick_s": 1 / os.sysconf("SC_CLK_TCK"),
            "voluntary_context_switches": after["voluntary"] - before["voluntary"],
            "involuntary_context_switches": after["involuntary"] - before["involuntary"],
            "threads_before": before["threads"], "threads_after": after["threads"],
            "snapshot_span_s": before["snapshot_span_s"] + after["snapshot_span_s"],
            "before": before, "after": after}


def validate_run(client, server, returncode, engine, app):
    """Recalculate protocol accounting and metrics; do not trust a lone 'valid' flag."""
    problems = []
    if not client.get("valid") or client.get("errors"):
        problems.append("client failed response validation")
    if returncode != 0 or not server or not server.get("server_ok"):
        return problems + ["server failed or produced no successful summary"]
    n = client["completed"]
    all_messages = n + client["active"] * client["warmup_per_active_connection"]
    size = client["payload_bytes"]
    expected_read = all_messages * (8 + (size if app != "download" else 0))
    expected_write = all_messages * (1 if app == "upload" else size)
    expected = {"engine": engine, "app": app, "messages": all_messages,
                "wire_bytes_read": expected_read, "wire_bytes_written": expected_write,
                "accepted": client["connections"], "closed": client["connections"],
                "peak": client["connections"], "errors": 0, "rejected": 0, "incomplete": 0}
    for key, value in expected.items():
        if server.get(key) != value:
            problems.append(f"server {key}: expected {value}, got {server.get(key)}")
    if server.get("max_rss_kib", -1) <= 0:
        problems.append("server RSS unavailable")
    samples = sorted(client["latencies_us"])
    if (n <= 0 or len(samples) != n or client["attempted"] != n or
            not all(math.isfinite(x) and x > 0 for x in samples)):
        problems.append("missing/invalid latency samples or attempted-count mismatch")
    per_connection = client.get("measured_per_connection", [])
    if (len(per_connection) != client["active"] or sum(per_connection) != n or
            any(value < client["requests_per_active_connection"] for value in per_connection)):
        problems.append("per-connection measured counts disagree")
    elapsed = client["elapsed_s"]
    if not math.isfinite(elapsed) or elapsed <= 0:
        problems.append("invalid measurement duration")
    elif samples:
        values = {"throughput_requests_s": n / elapsed, "payload_mib_s": n * size / 1048576 / elapsed}
        values.update({key: samples[math.ceil(p * len(samples)) - 1]
                       for key, p in (("p50_us", .5), ("p95_us", .95), ("p99_us", .99))})
        for key, value in values.items():
            if not math.isclose(client[key], value, rel_tol=1e-10):
                problems.append(f"client {key} disagrees with raw data")
    measured = client.get("server_measurement", {})
    if measured:
        before, after = measured["before"], measured["after"]
        cpu = (after["cpu_ticks"] - before["cpu_ticks"]) * measured["cpu_tick_s"]
        interval = after["time"] - before["time"]
        if interval <= 0 or cpu < 0 or not math.isclose(measured["cpu_s"], cpu, rel_tol=1e-10):
            problems.append("server CPU delta disagrees with raw snapshots")
        elif not math.isclose(measured["cpu_percent"], 100 * cpu / interval, rel_tol=1e-10):
            problems.append("server CPU percentage disagrees with snapshot interval")
        if before["threads"] != 1 or after["threads"] != 1:
            problems.append("observed server application thread count is not one")
    return problems


def load_guard_issues(client):
    issues = []
    if not math.isfinite(client["client_cpu_percent"]) or client["client_cpu_percent"] >= 85:
        issues.append("load generator uses >=85% of its core; compare worker counts before inferring a server maximum")
    rate = client.get("rate_requested_s", 0)
    if rate and client["throughput_requests_s"] < .95 * rate:
        issues.append("requested load not sustained (below 95% of --rate); investigate client/server limits")
    return issues


def quality_checks(client, min_seconds, min_samples, traced, load_policy="warn"):
    issues = []
    warnings = []
    if client["elapsed_s"] < min_seconds:
        issues.append(f"duration below configured {min_seconds:g}s minimum")
    if client["completed"] < min_samples:
        issues.append(f"sample count below configured {min_samples} minimum")
    if not traced:
        (issues if load_policy == "strict" else warnings).extend(load_guard_issues(client))
    if traced:
        issues.append("traced run: never include in untraced performance comparisons")
    if client["client_cpu_percent"] >= 80:
        warnings.append("client uses >=80% of one CPU; investigate client capacity before inferring a server limit")
    resources = client.get("server_measurement", {})
    if resources and resources["interval_s"] < 100 * resources["cpu_tick_s"]:
        warnings.append("short CPU interval: kernel tick quantization materially affects small CPU totals")
    return issues, warnings


def parse_strace(path):
    """Parse the C-locale default strace -c summary, including optional error counts."""
    rows = []
    total_calls = None
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) not in (5, 6) or not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", parts[0]):
            continue
        try:
            row = {"syscall": parts[-1], "calls": int(parts[3]),
                   "errors": int(parts[4]) if len(parts) == 6 else 0,
                   "estimated_cpu_s": float(parts[1])}
        except ValueError:
            continue
        if row["syscall"] == "total":
            total_calls = row["calls"]
        else:
            rows.append(row)
    if not rows or total_calls is None or sum(row["calls"] for row in rows) != total_calls:
        raise ValueError("Missing, unsupported, or inconsistent strace summary; inspect syscalls.txt")
    return {"total_calls": total_calls, "syscalls": rows,
            "scope": "Full traced server lifetime, including startup/warmup/shutdown. strace CPU seconds are estimates under instrumentation, not untraced overhead."}


# ----- Repeatable experiments: untraced benchmarks and separate syscall profiles -----


def profile_main(argv):
    return benchmark_main(argv, traced=True)


def benchmark_main(argv, traced=False):
    parser = argparse.ArgumentParser(description="Separate correctness from measurement quality; keep every attempt")
    parser.add_argument("--engines", nargs="+", choices=ENGINES, default=list(ENGINES))
    parser.add_argument("--app", choices=APPS, default="echo")
    parser.add_argument("--connections", nargs="+", type=int, default=[10, 100])
    parser.add_argument("--active", type=int, help="Default: all open connections generate traffic")
    parser.add_argument("--requests", type=int, default=100, help="Minimum requests per active connection")
    parser.add_argument("--rate", type=float, default=0, help="Target total requests/s; default 0 is unpaced closed-loop traffic")
    parser.add_argument("--duration", type=float, default=5, help="Minimum measured seconds per repetition")
    parser.add_argument("--bytes", type=int, default=64, dest="size")
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--min-seconds", type=float, default=5)
    parser.add_argument("--min-samples", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=16)
    parser.add_argument("--client-cpu", type=int)
    parser.add_argument("--server-cpu", type=int)
    parser.add_argument("--out", type=Path, default=ROOT / "chirag/results" / time.strftime("%Y%m%d-%H%M%S"))
    args = parser.parse_args(argv)
    if (args.runs < 1 or args.requests < 1 or args.warmup < 0 or min(args.connections) < 1 or
            (args.active is not None and not 1 <= args.active <= min(args.connections)) or
            not 1 <= args.size <= MAX_TRANSFER or (args.app == "echo" and args.size > 4096) or
            not math.isfinite(args.duration) or args.duration < 0 or
            not math.isfinite(args.min_seconds) or args.min_seconds <= 0 or args.min_samples < 1):
        parser.error("Invalid workload or measurement-quality settings")
    if len(set(args.connections)) != len(args.connections) or len(set(args.engines)) != len(args.engines):
        parser.error("Duplicate engines/connection counts would repeat the same job name")
    if not math.isfinite(args.rate) or args.rate < 0:
        parser.error("--rate must be finite and non-negative")
    affinity = os.sched_getaffinity(0)
    args.server_cpu, chosen_clients, topology = capacity_cpus(
        args.server_cpu, [args.client_cpu] if args.client_cpu is not None else None, 1)
    args.client_cpu = chosen_clients[0]
    if any(cpu is not None and cpu not in affinity for cpu in (args.client_cpu, args.server_cpu)):
        parser.error("Requested CPU is outside this process's available affinity set")
    if traced and not shutil.which("strace"):
        parser.error("strace is required for profile")
    try:
        check_consistency()
    except ValueError as exc:
        parser.error(str(exc))
    args.out.mkdir(parents=True, exist_ok=False)
    builds = {}
    for engine in args.engines:
        path = ROOT / "bin" / f"{engine}.build.json"
        build = json.loads(path.read_text()) if path.exists() else None
        if build and (not (ROOT / "bin" / f"{engine}_server").exists() or
                      build["binary_sha256"] != digest(ROOT / "bin" / f"{engine}_server") or
                      build["source_sha256"] != digest(ROOT / build["source"])):
            build = None
        builds[engine] = build
    metadata = {
        "schema_version": 2, "traced": traced,
        "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
        "platform": platform.platform(), "python": platform.python_version(),
        "logical_cpus": os.cpu_count(), "available_affinity": sorted(affinity),
        "source_sha256": source_hashes(), "builds": builds,
        "binary_sha256": {e: digest(ROOT / "bin" / f"{e}_server") for e in args.engines
                           if (ROOT / "bin" / f"{e}_server").exists()},
        "resource_scope": "CPU/context-switch deltas bracket measured client traffic; CPU resolution is SC_CLK_TCK. Peak RSS and final server counters are lifetime values.",
        "traffic_scope": "Connection setup and warmup excluded; validation included. Closed-loop: one request in flight per active connection.",
        "topology": "Client and server pinned to distinct physical cores; tracer stays on client core. Affinity does not reserve cores from the OS.",
        "cpu_topology": topology, "load_policy": "warn",
        "load_guard": "CPU >=85% or achieved rate <95% of target are interpretation warnings, not correctness failures",
        "quality_policy": "Minimum duration/sample thresholds are transparent heuristics, not statistical guarantees. Analysis separately requires repeated runs.",
    }
    write_json(args.out / "metadata.json", metadata)
    jobs = [(e, c, r) for e in args.engines for c in args.connections for r in range(1, args.runs + 1)]
    random.Random(args.seed).shuffle(jobs)
    rows = []
    try:
        if args.client_cpu is not None:
            os.sched_setaffinity(0, {args.client_cpu})
        for engine, connections, repetition in jobs:
            folder = args.out / f"{engine}-{connections}-run{repetition}"
            folder.mkdir()
            server = None
            result = {"engine": engine, "app": args.app, "connections": connections, "run": repetition,
                      "valid": False, "measurement_usable": False, "traced": traced,
                      "payload_bytes": args.size, "active": args.active or connections}
            try:
                server = RunningServer(engine, args.app, folder, profile=traced, cpu=args.server_cpu)
                if args.server_cpu is None:
                    os.sched_setaffinity(server.pid, affinity)
                client = asyncio.run(run_load("127.0.0.1", server.port, app=args.app, connections=connections,
                                              active=args.active, requests=args.requests, size=args.size,
                                              warmup=args.warmup, duration=args.duration, server_pid=server.pid, rate=args.rate))
                summary = server.stop()
                write_json(folder / "client.json", client)
                write_json(folder / "server.json", summary)
                result["server_exit_code"] = server.returncode
                problems = validate_run(client, summary, server.returncode, engine, args.app)
                issues, warnings = quality_checks(client, args.min_seconds, args.min_samples, traced)
                if builds[engine] is None:
                    issues.append("missing/stale build manifest; use the build command to record the exact binary provenance")
                result.update(valid=not problems, completed=client["completed"],
                              quality_issues=issues, warnings=warnings, validation_errors=problems)
                if not problems:
                    for key in ("elapsed_s", "throughput_requests_s", "payload_mib_s", "p50_us", "p95_us", "p99_us", "client_cpu_percent"):
                        result[key] = client[key]
                    measured = client["server_measurement"]
                    result.update(server_cpu_percent=measured["cpu_percent"], server_measured_cpu_s=measured["cpu_s"],
                                  server_resource_interval_s=measured["interval_s"],
                                  server_cpu_us_per_request=measured["cpu_s"] * 1e6 / client["completed"],
                                  server_voluntary_context_switches=measured["voluntary_context_switches"],
                                  server_involuntary_context_switches=measured["involuntary_context_switches"],
                                  server_peak_rss_kib=summary["max_rss_kib"],
                                  server_lifetime_cpu_s=summary["user_cpu_s"] + summary["system_cpu_s"])
                    if traced:
                        profile = parse_strace(folder / "syscalls.txt")
                        profile["calls_per_message"] = profile["total_calls"] / summary["messages"]
                        profile["controller_lifetime_wall_s"] = server.lifetime_wall_s
                        profile["calls_per_controller_lifetime_s"] = profile["total_calls"] / server.lifetime_wall_s
                        write_json(folder / "syscalls.json", profile)
                        result["syscall_total"] = profile["total_calls"]
                        result["syscalls_per_message"] = profile["calls_per_message"]
                    result["measurement_usable"] = not issues
                else:
                    result["error"] = "; ".join(problems)
            except Exception as exc:
                result.update(valid=False, measurement_usable=False, error=f"{type(exc).__name__}: {exc}")
                # Do not let a profiling/parser failure leave apparently successful metrics behind.
                for key in list(result):
                    if key in PERFORMANCE_FIELDS:
                        result.pop(key)
            finally:
                if server:
                    server.stop()
            write_json(folder / "result.json", result)
            rows.append(result)
            print(json.dumps(public_server_row(result)), flush=True)
    finally:
        os.sched_setaffinity(0, affinity)
    write_rows(args.out / "summary.csv", [public_server_row(r) for r in rows])
    print(f"Saved: {args.out}")
    # Correct but too-short untraced data has a distinct exit status, never a fake correctness failure.
    return 1 if any(not row["valid"] for row in rows) else (0 if traced or all(row["measurement_usable"] for row in rows) else 2)


PERFORMANCE_FIELDS = ("elapsed_s", "throughput_requests_s", "payload_mib_s", "p50_us", "p95_us", "p99_us",
                      "client_cpu_percent", "server_cpu_percent", "server_measured_cpu_s",
                      "server_resource_interval_s", "server_cpu_us_per_request", "server_voluntary_context_switches",
                      "server_involuntary_context_switches", "server_peak_rss_kib", "server_lifetime_cpu_s",
                      "syscall_total", "syscalls_per_message")


def public_server_row(row):
    return {k: v for k, v in row.items() if k not in
            ("client_cpu_percent", "start_skew_ms", "finish_skew_ms")}


def write_rows(path, rows):
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with Path(path).open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows({key: json.dumps(value) if isinstance(value, (list, dict)) else value
                          for key, value in row.items()} for row in rows)


# ----- Analysis and figures from validated repetitions -----


def load_suite(path):
    path = Path(path)
    metadata = json.loads((path / "metadata.json").read_text())
    if metadata.get("schema_version") != 2:
        raise ValueError(f"{path}: use new schema-2 experiments; historical smoke/audit evidence remains unchanged")
    rows = []
    for filename in sorted(path.glob("*/result.json")):
        row = json.loads(filename.read_text())
        row["folder"] = str(filename.parent)
        if row["valid"]:
            client = json.loads((filename.parent / "client.json").read_text())
            server = json.loads((filename.parent / "server.json").read_text())
            problems = validate_run(client, server, row["server_exit_code"], row["engine"], row["app"])
            for key in ("elapsed_s", "throughput_requests_s", "payload_mib_s", "p50_us", "p95_us", "p99_us", "client_cpu_percent"):
                if not math.isclose(row[key], client[key], rel_tol=1e-10):
                    problems.append(f"saved result {key} changed or disagrees with client")
            measured = client["server_measurement"]
            if "load_guard" in metadata:
                if client.get("client_cpu_affinity") != [metadata["settings"]["client_cpu"]]:
                    problems.append("Client affinity differs from its separate core")
                if client.get("rate_requested_s") != metadata["settings"].get("rate"):
                    problems.append("Recorded target rate differs from experiment settings")
            if "load_guard" in metadata and any(measured[t].get("affinity") != [metadata["settings"]["server_cpu"]] for t in ("before", "after")):
                problems.append("Server affinity differs from recorded dedicated core")
            expected_resources = {
                "server_cpu_percent": measured["cpu_percent"],
                "server_measured_cpu_s": measured["cpu_s"],
                "server_resource_interval_s": measured["interval_s"],
                "server_cpu_us_per_request": measured["cpu_s"] * 1e6 / client["completed"],
                "server_peak_rss_kib": server["max_rss_kib"],
                "server_lifetime_cpu_s": server["user_cpu_s"] + server["system_cpu_s"],
                "server_voluntary_context_switches": measured["after"]["voluntary"] - measured["before"]["voluntary"],
                "server_involuntary_context_switches": measured["after"]["involuntary"] - measured["before"]["involuntary"],
            }
            for key, value in expected_resources.items():
                if not math.isclose(row[key], value, rel_tol=1e-10):
                    problems.append(f"saved {key} disagrees with raw resource evidence")
            if row["traced"] != metadata["traced"]:
                problems.append("trace mode disagrees with metadata")
            if problems:
                raise ValueError(f"{filename}: " + "; ".join(problems))
            issues, _ = quality_checks(client, metadata["settings"]["min_seconds"],
                                       metadata["settings"]["min_samples"], metadata["traced"],
                                       metadata.get("load_policy", "strict"))
            if row["measurement_usable"] and (issues or row["quality_issues"]):
                raise ValueError(f"{filename}: inconsistent measurement quality")
        rows.append(row)
    settings = metadata["settings"]
    expected = {(e, c, r) for e in settings["engines"] for c in settings["connections"]
                for r in range(1, settings["runs"] + 1)}
    actual = [(r["engine"], r["connections"], r["run"]) for r in rows]
    if len(actual) != len(set(actual)) or set(actual) != expected:
        raise ValueError("Experiment matrix is incomplete or contains duplicate/unexpected attempts")
    return metadata, rows


def latency_histogram_plots(groups, output, pooled):
    """Common latency buckets, separate server panels, all measured samples included."""
    import bisect
    import matplotlib.pyplot as plt
    figures = []
    colours = dict(zip(ENGINES, ("#0072B2", "#E69F00", "#009E73", "#CC79A7")))
    for condition in sorted({key[1:] for key in groups}):
        samples_by_engine = {}
        for key, repetitions in groups.items():
            if key[1:] != condition:
                continue
            samples = []
            for row in repetitions:
                folder = Path(row["folder"])
                files = [folder / f"client-{i}.json" for i in range(row["workers"])] if pooled else [folder / "client.json"]
                for file in files:
                    samples.extend(json.loads(file.read_text())["latencies_us"])
            samples_by_engine[key[0]] = sorted(samples)
        # Ten equal-width ordinary buckets and an explicit overflow bucket keep
        # outliers visible without compressing the entire distribution into one bar.
        upper = max(values[math.ceil(.99 * len(values)) - 1] for values in samples_by_engine.values())
        unit = 10 ** math.floor(math.log10(upper / 10))
        upper = 10 * math.ceil((upper / 10) / unit) * unit
        edges = [upper * i / 10 for i in range(1, 11)]
        decimals = max(0, -math.floor(math.log10(upper / 10)))
        labels = [f"{upper*i/10:,.{decimals}f}–{upper*(i+1)/10:,.{decimals}f}" for i in range(10)] + [f"≥{upper:,.{decimals}f}"]
        fig, axes = plt.subplots(2, 2, figsize=(14, 9), sharey=True, layout="constrained")
        for ax, engine in zip(axes.flat, ENGINES):
            values = samples_by_engine.get(engine)
            if not values:
                ax.set_visible(False)
                continue
            counts = [0] * 11
            for value in values:
                counts[bisect.bisect_right(edges, value)] += 1
            assert sum(counts) == len(values)
            ax.bar(range(11), [100 * count / len(values) for count in counts],
                   color=colours[engine], edgecolor="white")
            ax.set_xticks(range(11), labels, rotation=45, ha="right")
            ax.set(title=f"{engine}: {len(values):,} replies", xlabel="Latency range (µs; last bar includes all larger values)",
                   ylabel="Replies in range (%)", ylim=(0, 100))
            ax.grid(axis="y", alpha=.25)
            ax.set_axisbelow(True)
        description = (f"{condition[0]} client processes" if pooled else
                       f"{condition[0]} open / {condition[1]} active connections")
        fig.suptitle(f"End-to-end latency distribution — {description}\n"
                     "Same buckets for all servers; measured replies pooled across eligible runs; no samples removed")
        name = "latency_histogram_" + "_".join(map(str, condition)) + ".png"
        fig.savefig(output / name, dpi=160)
        plt.close(fig)
        figures.append(name)
    return figures


def analyze_main(argv):
    parser = argparse.ArgumentParser(description="Validate saved evidence, summarize repetitions, generate figures and report")
    parser.add_argument("--input", type=Path, required=True, help="Untraced benchmark folder")
    parser.add_argument("--profile", type=Path, help="Optional separate syscall-profile folder")
    parser.add_argument("--out", type=Path, required=True, help="New analysis output directory")
    parser.add_argument("--min-runs", type=int, default=3)
    args = parser.parse_args(argv)
    if args.min_runs < 2:
        parser.error("At least two repetitions are required; use three or more for final experiments")
    metadata, rows = load_suite(args.input)
    if metadata["traced"]:
        parser.error("--input must be untraced; provide tracing separately with --profile")
    profile_rows = []
    if args.profile:
        profile_meta, profile_rows = load_suite(args.profile)
        if not profile_meta["traced"]:
            parser.error("--profile must be a traced experiment")
        for key in ("app", "size", "connections", "active", "engines", "client_cpu", "server_cpu", "rate"):
            if profile_meta["settings"].get(key) != metadata["settings"].get(key):
                parser.error(f"Profile workload/topology differs: {key}")
        if profile_meta["binary_sha256"] != metadata["binary_sha256"]:
            parser.error("Profile and benchmark used different server binaries")
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        parser.error("Plot generation requires matplotlib (for example the python3-matplotlib package)")
    args.out.mkdir(parents=True, exist_ok=False)
    groups = {}
    excluded = []
    for row in rows:
        key = (row["engine"], row["connections"], row["active"])
        groups.setdefault(key, [])
        if row["valid"] and row["measurement_usable"]:
            groups[key].append(row)
        else:
            excluded.append({"folder": row["folder"], "reason": row.get("error") or "; ".join(row.get("quality_issues", []))})
    summaries = []
    eligible = {}
    metrics = ("throughput_requests_s", "payload_mib_s", "p50_us", "p95_us", "p99_us",
               "server_cpu_percent", "server_cpu_us_per_request", "server_peak_rss_kib",
               "server_voluntary_context_switches", "server_involuntary_context_switches")
    for key, repetitions in groups.items():
        if len(repetitions) < args.min_runs:
            excluded.append({"group": str(key), "reason": f"only {len(repetitions)} usable repetitions; require {args.min_runs}"})
            continue
        eligible[key] = repetitions
        summary = {"engine": key[0], "connections": key[1], "active": key[2], "runs": len(repetitions)}
        for metric in metrics:
            values = [r[metric] for r in repetitions]
            summary.update({metric + "_median": statistics.median(values), metric + "_min": min(values),
                            metric + "_max": max(values)})
        summaries.append(summary)
    write_rows(args.out / "aggregates.csv", summaries)
    write_json(args.out / "exclusions.json", excluded)
    # Use consistent colours across every figure, even if an engine is excluded.
    colours = dict(zip(ENGINES, ("#0072B2", "#E69F00", "#009E73", "#CC79A7")))

    def comparison_plot(data, label, title, filename):
        # Each item contains engine, connection count, median, minimum, maximum.
        counts = sorted({item[1] for item in data})
        engines = [engine for engine in ENGINES if any(item[0] == engine for item in data)]
        fig, ax = plt.subplots(figsize=(9, 5.5), layout="constrained")
        width = 0.8 / len(engines)
        for index, engine in enumerate(engines):
            values = sorted((item for item in data if item[0] == engine), key=lambda item: item[1])
            middle = [item[2] for item in values]
            positions = [counts.index(item[1]) + (index - (len(engines) - 1) / 2) * width
                         for item in values]
            ax.bar(positions, middle, width=width,
                   color=colours[engine], label=engine, edgecolor="white")
        ax.set_xticks(range(len(counts)), [str(count) for count in counts])
        ax.set(xlabel="Open connections", ylabel=label, title=title)
        ax.set_ylim(bottom=0)
        ax.set_axisbelow(True)
        ax.grid(axis="y", alpha=.25)
        ax.legend(title="Server")
        fig.savefig(args.out / filename, dpi=160)
        plt.close(fig)

    figures = []
    for metric, label, filename in (
            ("throughput_requests_s", "Verified server replies / second (client-observed)", "throughput.png"),
            ("payload_mib_s", "Application payload throughput (MiB/s)", "payload_throughput.png"),
            ("p50_us", "End-to-end request latency p50 (microseconds)", "latency_p50.png"),
            ("p95_us", "End-to-end request latency p95 (microseconds)", "latency_p95.png"),
            ("p99_us", "End-to-end request latency p99 (microseconds)", "latency_p99.png"),
            ("server_cpu_us_per_request", "Server measured CPU microseconds / request", "cpu_efficiency.png"),
            ("server_cpu_percent", "Server CPU % of one core (measured interval)", "server_cpu.png"),
            ("server_peak_rss_kib", "Server process lifetime peak RSS (KiB)", "memory.png")):
        if not summaries:
            break
        if metric == "payload_mib_s" and metadata["settings"]["app"] == "echo":
            continue  # At fixed echo size this duplicates the replies/second curve.
        data = [(r["engine"], r["connections"], r[metric + "_median"],
                 r[metric + "_min"], r[metric + "_max"]) for r in summaries]
        comparison_plot(data, label,
                        f"{label}\n{metadata['settings']['app']} — median across repetitions", filename)
        figures.append(filename)
    figures.extend(latency_histogram_plots(eligible, args.out, pooled=False))
    syscalls = []
    profile_totals = {}
    for row in profile_rows:
        if not row["valid"]:
            continue
        profile = parse_strace(Path(row["folder"]) / "syscalls.txt")
        server = json.loads((Path(row["folder"]) / "server.json").read_text())
        profile_totals.setdefault((row["engine"], row["connections"]), []).append(profile["total_calls"] / server["messages"])
        saved_profile = json.loads((Path(row["folder"]) / "syscalls.json").read_text())
        for call in profile["syscalls"]:
            syscalls.append({"engine": row["engine"], "connections": row["connections"], "run": row["run"],
                             **call, "calls_per_server_message": call["calls"] / server["messages"],
                             "calls_per_controller_lifetime_s": call["calls"] / saved_profile["controller_lifetime_wall_s"]})
    write_rows(args.out / "syscall_frequencies.csv", syscalls)
    if profile_totals:
        data = [(engine, connections, statistics.median(values), min(values), max(values))
                for (engine, connections), values in profile_totals.items()]
        comparison_plot(data, "Full-lifetime traced syscalls / completed server message",
                        "Server syscall frequency — median across traced repetitions\n"
                        "Includes startup, warmup and shutdown", "syscalls.png")
        figures.append("syscalls.png")
    lines = ["# Generated benchmarking report", "", "## Scope and evidence", "",
             "This report describes the recorded experiments, not a universal ranking or a completed literature review.", "",
             f"Workload: **{metadata['settings']['app']}**, payload **{metadata['settings']['size']} bytes**.",
             f"Configured open connections: {metadata['settings']['connections']}; active: {metadata['settings']['active'] or 'all open'}.",
             f"Requested duration: {metadata['settings']['duration']}s minimum; requests: {metadata['settings']['requests']} minimum per active connection.",
             f"Target total requests/s: {metadata['settings'].get('rate', 0)} (0 means unpaced). Nonzero rate is controlled load; zero is unpaced closed-loop throughput. Neither alone proves maximum server capacity.",
             f"Warmup: {metadata['settings']['warmup']} per active connection; configured repetitions: {metadata['settings']['runs']}.",
             f"Quality thresholds: {metadata['settings']['min_seconds']}s and {metadata['settings']['min_samples']} measured samples; analysis requires {args.min_runs} usable repetitions.",
             f"Platform: {metadata['platform']}. Python: {metadata['python']}.",
             f"Client CPU affinity option: {metadata['settings']['client_cpu']}; server: {metadata['settings']['server_cpu']}.",
             "", "The quality thresholds are declared heuristics, not confidence guarantees. Raw evidence retains rejected/short attempts.",
             "", "## Results", "", "| Engine | Open | Active | Usable runs | Median requests/s | Server CPU % | Median CPU us/request | Median peak RSS KiB |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for r in summaries:
        lines.append(f"| {r['engine']} | {r['connections']} | {r['active']} | {r['runs']} | {r['throughput_requests_s_median']:.2f} | {r['server_cpu_percent_median']:.2f} | {r['server_cpu_us_per_request_median']:.3f} | {r['server_peak_rss_kib_median']:.0f} |")
    if not summaries:
        lines.extend(["", "**No group met the configured evidence criteria. No performance charts or ranking were generated.**"])
    lines.extend(["", "Graphs show median performance without error bars. Minimum and maximum values remain in aggregates.csv and the syscall table; consult them when discussing repeatability and outliers.",
                  "All comparison figures use grouped bar charts, including connection scaling. Bar axes start at zero; colours identify the same servers throughout. Latency distributions use separate server panels with identical latency-range bars, including an overflow bar; all measured samples are included.",
                  "Server resource figures and end-to-end request latency distributions are presented. Latency includes client scheduling, TCP and validation; it is not isolated server processing time. Client CPU is diagnostic only.",
                  "", "## Warnings and exclusions", ""])
    for row in rows:
        for warning in row.get("warnings", []):
            lines.append(f"- {Path(row['folder']).name}: {warning}")
    for item in excluded:
        lines.append(f"- {item.get('group') or item.get('folder')}: {item['reason']}")
    if not excluded and not any(r.get("warnings") for r in rows):
        lines.append("No automatic warnings or exclusions. This does not establish absence of all confounding factors.")
    lines.extend(["", "## Syscall profiling", ""])
    if syscalls:
        lines.extend(["See syscall_frequencies.csv for every observed syscall, count, error count, estimated traced CPU seconds, and calls per server message.",
                      "Counts cover full traced server lifetime, including startup, warmup, and shutdown. Instrumented timings are not used in the performance figures.",
                      "strace timing estimates are not isolated untraced syscall overhead. io_uring operations are not one syscall each; inspect io_uring_enter/setup rather than treating CQEs as syscall counts."])
        lines.extend(["", "| Engine | Open | Traced repetitions | Median calls/message | Min | Max |",
                      "| --- | ---: | ---: | ---: | ---: | ---: |"])
        for (engine, connections), values in sorted(profile_totals.items()):
            lines.append(f"| {engine} | {connections} | {len(values)} | {statistics.median(values):.3f} | {min(values):.3f} | {max(values):.3f} |")
        failed_profiles = [r for r in profile_rows if not r["valid"]]
        lines.append(f"\nInvalid traced attempts excluded: {len(failed_profiles)}. A single traced repetition has no measured between-run variation.")
    else:
        lines.append("No validated syscall profile was supplied. No syscall-frequency claim is made.")
    lines.extend(["", "## Interpretation and limits", "",
                  "- Client response validation is timed. This is closed-loop, end-to-end local traffic, not a production open-loop latency study.",
                  "- Server CPU/context-switch snapshots bracket measured traffic; CPU is quantized in kernel ticks. Exact snapshot values are in client.json.",
                  "- Peak RSS is process-lifetime VmHWM; it includes warmup and excludes a full kernel-memory accounting. Do not describe it as total system memory.",
                  "- Context switches are scheduler counters, not counts of user/kernel mode transitions. Syscalls do not necessarily switch tasks.",
                  "- Client saturation and missed rate targets are warnings. Correct measured results remain visible, but cannot by themselves establish maximum server capacity.",
                  "- select/poll scanning, epoll registration, and io_uring batching differ. These are possible mechanisms to investigate, not causal explanations proven by these graphs.",
                  "- No universal io_uring superiority is asserted. ET epoll, SQPOLL, and the historical CPU investigation are excluded from this implementation.",
                  "", "## Figures", ""])
    for name in figures:
        lines.append(f"![{name}]({name})\n")
    write_json(args.out / "analysis_provenance.json", {
        "source_suite": str(args.input.resolve()), "metadata": metadata,
        "profile_suite": str(args.profile.resolve()) if args.profile else None,
        "analysis_command": sys.argv, "minimum_runs": args.min_runs,
        "input_sha256": {str(p.resolve()): digest(p) for folder in [args.input, args.profile] if folder
                         for p in Path(folder).rglob('*') if p.is_file() and p.suffix in ('.json', '.csv', '.txt')},
        "analyzer_sha256": digest(Path(__file__)), "matplotlib_version": matplotlib.__version__,
    })
    (args.out / "report.md").write_text("\n".join(lines) + "\n")
    print(f"Report and {len(figures)} figures: {args.out}")
    return 0 if len(eligible) == len(groups) and not excluded else 2


# ----- Measurement regression tests (no network required) -----


def selftest_main(argv):
    parser = argparse.ArgumentParser(description="Test metric validation, quality gates, and syscall-summary parsing")
    parser.parse_args(argv)
    check_consistency()
    client = {"valid": True, "errors": [], "active": 2, "connections": 2, "completed": 4,
              "attempted": 4, "requests_per_active_connection": 2, "warmup_per_active_connection": 1,
              "payload_bytes": 64, "latencies_us": [10, 20, 30, 40], "elapsed_s": 2,
              "throughput_requests_s": 2, "payload_mib_s": 128 / 1048576,
              "p50_us": 20, "p95_us": 40, "p99_us": 40, "client_cpu_percent": 10,
              "measured_per_connection": [2, 2]}
    for app in APPS:
        server = {"engine": "select", "app": app, "server_ok": True, "messages": 6,
                  "wire_bytes_read": 6 * (8 + (64 if app != 'download' else 0)),
                  "wire_bytes_written": 6 * (1 if app == 'upload' else 64),
                  "accepted": 2, "closed": 2, "peak": 2, "errors": 0, "rejected": 0,
                  "incomplete": 0, "max_rss_kib": 1000}
        assert not validate_run(client, server, 0, "select", app)
        for key in ("messages", "wire_bytes_read", "wire_bytes_written", "accepted", "closed", "peak"):
            bad = dict(server); bad[key] += 1
            assert validate_run(client, bad, 0, "select", app), key
        for key in ("throughput_requests_s", "payload_mib_s", "p50_us", "p95_us", "p99_us"):
            bad = dict(client); bad[key] += 1
            assert validate_run(bad, server, 0, "select", app), key
        assert validate_run(client, server, 1, "select", app)
    saturated = dict(client, client_cpu_percent=100, rate_requested_s=100)
    assert not quality_checks(saturated, 1, 4, False)[0]
    assert len(quality_checks(saturated, 1, 4, False)[1]) >= 2
    assert quality_checks(saturated, 1, 4, False, "strict")[0]
    assert load_guard_issues(dict(client, client_cpu_percent=90))
    assert load_guard_issues(dict(client, rate_requested_s=100))
    assert not load_guard_issues(dict(client, rate_requested_s=2))
    assert not quality_checks(client, 1, 4, False)[0]
    assert quality_checks(client, 3, 4, False)[0]
    assert quality_checks(client, 1, 5, False)[0]
    assert quality_checks(client, 1, 4, True)[0]
    with tempfile.TemporaryDirectory(prefix="v4-metric-test-") as tmp:
        path = Path(tmp) / "strace.txt"
        path.write_text("% time seconds usecs/call calls errors syscall\n50.00 0.001000 10 100 2 recvfrom\n50.00 0.001000 10 100 sendto\n100.00 0.002000 10 200 2 total\n")
        assert parse_strace(path)["total_calls"] == 200
        path.write_text(path.read_text().replace("200 2 total", "199 2 total"))
        try:
            parse_strace(path)
        except ValueError:
            pass
        else:
            raise AssertionError("Inconsistent strace totals were accepted")
    capacity_selftest()
    print("PASS: capacity pooling/corruption checks, application consistency, all three byte-accounting modes, corrupted metrics/counters, failed exit, quality/tracing gates, and syscall parser")
    return 0


# ----- Client-capacity experiments: fixed total connections, independent clients -----


def capacity_worker(channel, port, connections, cpu, settings, output):
    """One spawned Python process; the server remains a separate single-thread program."""
    try:
        os.sched_setaffinity(0, {cpu})

        async def ready():
            channel.send(("ready", os.getpid()))
            start_at = channel.recv()
            await asyncio.sleep(max(0, start_at - time.perf_counter()))

        def done():
            # Pause before socket cleanup, sorting, or JSON writing. The controller
            # snapshots the server once every worker has ended measured traffic.
            channel.send(("done", os.getpid()))
            if channel.recv() != "release":
                raise ValueError("Missing controller release")

        client = asyncio.run(run_load("127.0.0.1", port, connections=connections,
                                      requests=settings["requests"], size=settings["size"],
                                      warmup=settings["warmup"], duration=settings["duration"],
                                      ready_hook=ready, done_hook=done,
                                      rate=settings.get("rate", 0) * connections / settings["connections"]))
        client["worker_cpu"] = cpu
        client["worker_pid"] = os.getpid()
        client["cpu_affinity"] = sorted(os.sched_getaffinity(0))
        write_json(Path(output), client)
        channel.send(("saved", str(output)))
    except BaseException as exc:
        try:
            channel.send(("error", f"{type(exc).__name__}: {exc}"))
        except (BrokenPipeError, EOFError):
            pass
        raise
    finally:
        channel.close()


def combine_capacity_clients(clients, resources):
    """Recalculate worker metrics, then pool samples using one shared wall interval."""
    if not clients:
        raise ValueError("No client evidence")
    samples, counts = [], []
    for c in clients:
        n = c["completed"]
        ordered = sorted(c["latencies_us"])
        start, end = c["measurement_started_s"], c["measurement_finished_s"]
        if (not c["valid"] or c["errors"] or n <= 0 or c["attempted"] != n or
                n != len(ordered) or n != sum(c["measured_per_connection"]) or
                len(c["measured_per_connection"]) != c["connections"] or c["active"] != c["connections"] or
                any(x < c["requests_per_active_connection"] for x in c["measured_per_connection"]) or
                not all(math.isfinite(x) and x > 0 for x in ordered) or
                not isinstance(start, (int, float)) or not isinstance(end, (int, float)) or
                not math.isfinite(start) or not math.isfinite(end) or end <= start or
                not math.isfinite(c["client_measured_cpu_s"]) or c["client_measured_cpu_s"] < 0):
            raise ValueError("Invalid worker samples, counts, errors, or timing")
        expected = {"elapsed_s": end - start, "throughput_requests_s": n / (end - start),
                    "payload_mib_s": n * c["payload_bytes"] / 1048576 / (end - start),
                    "client_cpu_percent": 100 * c["client_measured_cpu_s"] / (end - start)}
        expected.update({name: ordered[math.ceil(p * n) - 1]
                         for name, p in (("p50_us", .5), ("p95_us", .95), ("p99_us", .99))})
        if any(not math.isclose(c[k], v, rel_tol=1e-9, abs_tol=1e-9) for k, v in expected.items()):
            raise ValueError("Worker metrics disagree with raw samples/timestamps")
        for key in ("app", "payload_bytes", "warmup_per_active_connection", "requests_per_active_connection", "duration_requested_s"):
            if c[key] != clients[0][key]:
                raise ValueError("Workers ran different workloads")
        if c["elapsed_s"] < c["duration_requested_s"]:
            raise ValueError("Worker ended before requested duration")
        samples.extend(ordered)
        counts.extend(c["measured_per_connection"])
    start = min(c["measurement_started_s"] for c in clients)
    end = max(c["measurement_finished_s"] for c in clients)
    elapsed = end - start
    ordered = sorted(samples)
    n = len(ordered)
    cpu = sum(c["client_measured_cpu_s"] for c in clients)
    merged = {key: clients[0][key] for key in ("app", "payload_bytes", "warmup_per_active_connection",
                                             "requests_per_active_connection", "duration_requested_s")}
    merged.update(valid=True, errors=[], completed=n, attempted=n, latencies_us=ordered,
                  connections=sum(c["connections"] for c in clients), active=len(counts),
                  measured_per_connection=counts, elapsed_s=elapsed,
                  throughput_requests_s=n / elapsed, payload_mib_s=n * merged["payload_bytes"] / 1048576 / elapsed,
                  client_measured_cpu_s=cpu, client_cpu_percent=100 * cpu / elapsed,
                  server_measurement=resources, measurement_started_s=start, measurement_finished_s=end)
    merged.update({name: ordered[math.ceil(p * n) - 1]
                   for name, p in (("p50_us", .5), ("p95_us", .95), ("p99_us", .99))})
    return merged


def capacity_metrics(clients, resources, server, exit_code, engine):
    merged = combine_capacity_clients(clients, resources)
    problems = validate_run(merged, server, exit_code, engine, "echo")
    if problems:
        raise ValueError("; ".join(problems))
    # Also check the resource interval and scheduler deltas, independently of saved summaries.
    recomputed = resource_difference(resources["before"], resources["after"])
    for key in ("cpu_s", "interval_s", "cpu_percent", "voluntary_context_switches", "involuntary_context_switches"):
        if not math.isclose(resources[key], recomputed[key], rel_tol=1e-9, abs_tol=1e-9):
            raise ValueError("Resource snapshots disagree with saved deltas")
    result = {key: merged[key] for key in ("completed", "elapsed_s", "throughput_requests_s", "payload_mib_s",
                                         "p50_us", "p95_us", "p99_us", "client_cpu_percent")}
    result.update(server_cpu_percent=resources["cpu_percent"],
                  server_cpu_us_per_request=resources["cpu_s"] * 1e6 / merged["completed"],
                  server_peak_rss_kib=server["max_rss_kib"],
                  start_skew_ms=1000 * (max(c["measurement_started_s"] for c in clients) - merged["measurement_started_s"]),
                  finish_skew_ms=1000 * (merged["measurement_finished_s"] - min(c["measurement_finished_s"] for c in clients)))
    return result


def capacity_cpus(server_cpu, client_cpus, workers):
    allowed = sorted(os.sched_getaffinity(0))
    topology = {}
    for cpu in allowed:
        folder = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
        topology[cpu] = (int((folder / "physical_package_id").read_text()),
                         int((folder / "core_id").read_text()))
    if server_cpu is None:
        server_cpu = allowed[0]
    if server_cpu not in allowed:
        raise ValueError("Server CPU is not available")
    if client_cpus is None:
        client_cpus, used = [], {topology[server_cpu]}
        for cpu in allowed:
            if topology[cpu] not in used:
                client_cpus.append(cpu)
                used.add(topology[cpu])
                if len(client_cpus) == workers:
                    break
    if len(client_cpus) < workers or any(c not in allowed for c in client_cpus):
        raise ValueError("Need one available physical core per client plus one for the server; reduce --workers")
    selected = [server_cpu, *client_cpus]
    if len({topology[c] for c in selected}) != len(selected):
        raise ValueError("Server and clients must use separate physical cores, not SMT siblings")
    return server_cpu, client_cpus, topology


def capacity_main(argv):
    original_affinity = os.sched_getaffinity(0)
    try:
        return capacity_run(argv)
    finally:
        os.sched_setaffinity(0, original_affinity)


def capacity_run(argv):
    parser = argparse.ArgumentParser(description="Test client capacity at fixed total connections; server stays single-threaded")
    parser.add_argument("--engines", nargs="+", choices=ENGINES, default=list(ENGINES))
    parser.add_argument("--workers", nargs="+", type=int, default=[4])
    parser.add_argument("--connections", type=int, default=100, help="TOTAL active connections, divided among workers")
    parser.add_argument("--rate", type=float, default=0, help="Target TOTAL requests/s divided among workers; default 0 is unpaced closed-loop traffic")
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--bytes", dest="size", type=int, default=64)
    parser.add_argument("--requests", type=int, default=100)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--server-cpu", type=int)
    parser.add_argument("--client-cpus", nargs="+", type=int)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    if (len(set(args.engines)) != len(args.engines) or len(set(args.workers)) != len(args.workers) or
            not math.isfinite(args.rate) or args.rate < 0 or min(args.workers) < 1 or max(args.workers) > args.connections or
            args.runs < 2 or not math.isfinite(args.duration) or args.duration <= 0 or
            not 1 <= args.size <= 4096 or args.requests < 1 or args.warmup < 0):
        parser.error("Use unique positive workers; workers <= total connections; runs >=2; positive duration and valid echo size")
    check_consistency()
    server_cpu, client_cpus, topology = capacity_cpus(args.server_cpu, args.client_cpus, max(args.workers))
    builds = {}
    for engine in args.engines:
        build = json.loads((ROOT / "bin" / f"{engine}.build.json").read_text())
        if (build["binary_sha256"] != digest(ROOT / "bin" / f"{engine}_server") or
                build["source_sha256"] != digest(ROOT / build["source"])):
            raise ValueError("Stale build manifest; run make before capacity experiments")
        builds[engine] = build
    settings = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "tool-source.txt").write_text(Path(__file__).read_text())
    write_json(args.out / "metadata.json", {"schema": "client-capacity-1", "load_policy": "warn", "settings": settings,
               "server_cpu": server_cpu, "client_cpus": client_cpus, "cpu_topology": topology,
               "platform": platform.platform(), "python": platform.python_version(), "builds": builds,
               "tool_sha256": digest(Path(__file__)),
               "scope": "Fixed total active echo connections; independent spawned clients on distinct physical cores; one server application thread. CPU snapshots bracket synchronized traffic, not JSON writing."})
    controller_candidates = [cpu for cpu, core in topology.items()
                             if core not in {topology[server_cpu], *(topology[c] for c in client_cpus)}]
    os.sched_setaffinity(0, {controller_candidates[0] if controller_candidates else client_cpus[0]})
    jobs = [(e, w, r) for e in args.engines for w in args.workers for r in range(1, args.runs + 1)]
    random.Random(16).shuffle(jobs)
    context = multiprocessing.get_context("spawn")
    results = []
    for engine, workers, repetition in jobs:
        folder = args.out / f"{engine}-workers{workers}-run{repetition}"
        folder.mkdir()
        server, children, channels = None, [], []
        row = dict(engine=engine, workers=workers, run=repetition, valid=False, measurement_usable=False)
        try:
            server = RunningServer(engine, "echo", folder, cpu=server_cpu)
            for index in range(workers):
                parent, child = context.Pipe()
                connections = args.connections // workers + (index < args.connections % workers)
                process = context.Process(target=capacity_worker,
                    args=(child, server.port, connections, client_cpus[index], settings, folder / f"client-{index}.json"))
                process.start()
                child.close()
                children.append(process)
                channels.append(parent)

            def wait_for(label, timeout):
                deadline = time.monotonic() + timeout
                for channel in channels:
                    if not channel.poll(max(0, deadline - time.monotonic())):
                        raise TimeoutError(f"Timed out waiting for worker {label}")
                    message = channel.recv()
                    if message[0] != label:
                        raise ValueError(f"Worker expected {label}, returned {message}")

            wait_for("ready", 60)
            before = process_snapshot(server.pid)
            # Same absolute monotonic target for every worker, after every warmup.
            for channel in channels:
                channel.send(before["time"] + .05)
            wait_for("done", args.duration + 120)
            after = process_snapshot(server.pid)
            for channel in channels:
                channel.send("release")
            wait_for("saved", 120)
            for process in children:
                process.join(timeout=10)
                if process.exitcode != 0:
                    raise ValueError("Client worker failed to exit successfully")
            summary = server.stop()
            resources = resource_difference(before, after)
            write_json(folder / "server.json", summary)
            write_json(folder / "resources.json", resources)
            clients = [json.loads((folder / f"client-{i}.json").read_text()) for i in range(workers)]
            row.update(capacity_metrics(clients, resources, summary, server.returncode, engine))
            warnings = [f"worker {i}: {issue}" for i, c in enumerate(clients) for issue in load_guard_issues(c)]
            issues = []
            if min(c["elapsed_s"] for c in clients) < 5 or row["completed"] < 1000:
                issues.append("Need >=5 seconds per worker and >=1000 total samples for performance analysis")
            if max(row["start_skew_ms"], row["finish_skew_ms"]) > 100:
                issues.append("Worker timing skew exceeds 100ms; inspect scheduling or slow requests")
            row.update(valid=True, measurement_usable=not issues, quality_issues=issues, warnings=warnings, server_exit_code=server.returncode)
        except (OSError, ValueError, RuntimeError, EOFError, subprocess.SubprocessError) as exc:
            row["error"] = f"{type(exc).__name__}: {exc}"
        finally:
            for process in children:
                if process.is_alive():
                    process.terminate()
                process.join(timeout=3)
                if process.is_alive():
                    process.kill()
                    process.join()
            for channel in channels:
                channel.close()
            if server:
                server.stop()
        write_json(folder / "result.json", row)
        results.append(row)
        print(json.dumps(public_server_row(row)), flush=True)
    write_rows(args.out / "summary.csv", [public_server_row(r) for r in results])
    return 1 if any(not r["valid"] for r in results) else (0 if all(r["measurement_usable"] for r in results) else 2)


def capacity_analyze_main(argv):
    parser = argparse.ArgumentParser(description="Revalidate and plot client-process scaling at fixed total connections")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--min-runs", type=int, default=3)
    args = parser.parse_args(argv)
    if args.min_runs < 2:
        parser.error("At least two repetitions required")
    meta = json.loads((args.input / "metadata.json").read_text())
    if meta.get("schema") != "client-capacity-1":
        raise ValueError("Use a capacity experiment folder")
    settings = meta["settings"]
    rows, groups, excluded, hashes = [], {}, [], {}
    for folder in sorted(args.input.iterdir()):
        if not folder.is_dir():
            continue
        row = json.loads((folder / "result.json").read_text())
        rows.append(row)
        if not row["valid"]:
            excluded.append({"folder": folder.name, "reason": row.get("error")})
            continue
        clients = [json.loads((folder / f"client-{i}.json").read_text()) for i in range(row["workers"])]
        if sum(c["connections"] for c in clients) != settings["connections"]:
            raise ValueError("Total connections changed between configurations")
        for i, c in enumerate(clients):
            if (c["worker_cpu"] != meta["client_cpus"][i] or c["payload_bytes"] != settings["size"] or
                    c["duration_requested_s"] != settings["duration"] or
                    c["requests_per_active_connection"] != settings["requests"] or
                    c["warmup_per_active_connection"] != settings["warmup"] or
                    ("rate" in settings and not math.isclose(c.get("rate_requested_s", -1), settings["rate"] * c["connections"] / settings["connections"], rel_tol=1e-9))):
                raise ValueError("Worker configuration disagrees with metadata")
        resource = json.loads((folder / "resources.json").read_text())
        server = json.loads((folder / "server.json").read_text())
        if "rate" in settings:
            if any(c.get("cpu_affinity") != [meta["client_cpus"][i]] for i, c in enumerate(clients)):
                raise ValueError("Worker affinity differs from selected core")
            if any(resource[when].get("affinity") != [meta["server_cpu"]] for when in ("before", "after")):
                raise ValueError("Server affinity differs from selected core")
        values = capacity_metrics(clients, resource, server, row["server_exit_code"], row["engine"])
        for key, value in values.items():
            if not math.isclose(row[key], value, rel_tol=1e-9, abs_tol=1e-9):
                raise ValueError(f"{folder.name}: altered metric {key}")
        usable = (min(c["elapsed_s"] for c in clients) >= 5 and values["completed"] >= 1000 and
                  max(values["start_skew_ms"], values["finish_skew_ms"]) <= 100)
        saved_guard = (any(load_guard_issues(c) for c in clients)
                       if "rate" in settings and meta.get("load_policy", "strict") == "strict" else False)
        if row["measurement_usable"] != (usable and not saved_guard):
            raise ValueError("Saved quality status disagrees with raw evidence")
        guard = [f"worker {i}: {issue}" for i, c in enumerate(clients) for issue in load_guard_issues(c)]
        row["warnings"] = guard
        row["folder"] = str(folder)
        if saved_guard:
            excluded.append({"folder": folder.name, "reason": guard})
            continue
        if not usable:
            excluded.append({"folder": folder.name, "reason": row["quality_issues"]})
            continue
        groups.setdefault((row["engine"], row["workers"]), []).append(row)
    expected = {(e, w, r) for e in settings["engines"] for w in settings["workers"] for r in range(1, settings["runs"] + 1)}
    actual = [(r["engine"], r["workers"], r["run"]) for r in rows]
    if set(actual) != expected or len(actual) != len(expected):
        raise ValueError("Capacity matrix incomplete or duplicated")
    summaries = []
    metrics = ("throughput_requests_s", "p50_us", "p95_us", "p99_us", "server_cpu_percent", "server_cpu_us_per_request", "server_peak_rss_kib")
    for engine in settings["engines"]:
        for workers in sorted(settings["workers"]):
            repetitions = groups.get((engine, workers), [])
            if len(repetitions) < args.min_runs:
                excluded.append({"group": [engine, workers], "reason": "Insufficient usable repetitions"})
                continue
            summary = dict(engine=engine, workers=workers, runs=len(repetitions))
            for metric in metrics:
                values = [r[metric] for r in repetitions]
                summary.update({metric + "_median": statistics.median(values), metric + "_min": min(values), metric + "_max": max(values)})
            summaries.append(summary)
    args.out.mkdir(parents=True, exist_ok=False)
    write_rows(args.out / "aggregates.csv", summaries)
    write_json(args.out / "exclusions.json", excluded)
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    colours = dict(zip(ENGINES, ("#0072B2", "#E69F00", "#009E73", "#CC79A7")))
    figures = []
    for metric, label in (("throughput_requests_s", "Verified server replies / second (client-observed)"),
                          ("p50_us", "End-to-end request latency p50 (microseconds)"),
                          ("p95_us", "End-to-end request latency p95 (microseconds)"),
                          ("p99_us", "End-to-end request latency p99 (microseconds)"),
                          ("server_cpu_percent", "Single-thread server CPU % of one core"),
                          ("server_peak_rss_kib", "Server process lifetime peak RSS (KiB)"),
                          ("server_cpu_us_per_request", "Server process CPU microseconds / request")):
        if not summaries:
            break
        fig, ax = plt.subplots(figsize=(10, 6), layout="constrained")
        worker_counts = sorted(settings["workers"])
        plotted_engines = [engine for engine in ENGINES if any(s["engine"] == engine for s in summaries)]
        bar_width = 0.8 / len(plotted_engines)
        for index, engine in enumerate(plotted_engines):
            values = sorted((s for s in summaries if s["engine"] == engine), key=lambda s: s["workers"])
            if not values:
                continue
            median = [v[metric + "_median"] for v in values]
            positions = ([index] if len(worker_counts) == 1 else
                         [worker_counts.index(v["workers"]) +
                          (index - (len(plotted_engines) - 1) / 2) * bar_width for v in values])
            ax.bar(positions, median, width=.65 if len(worker_counts) == 1 else bar_width,
                   color=colours[engine], edgecolor="white", label=engine)
        ax.set(xlabel="Independent client processes (total connections held fixed)", ylabel=label,
               title=f"Server comparison: {settings['connections']} connections, {settings['size']}-byte echo\n"
                     + (f"Target: {settings['rate']:g} requests/s" if settings.get("rate", 0) else "Unpaced traffic") + " | Median across repetitions", ylim=(0, None))
        if len(worker_counts) == 1:
            ax.set_xticks(range(len(plotted_engines)), plotted_engines)
            ax.set_xlabel(f"Single-threaded server ({worker_counts[0]} separate load-generator processes)")
        else:
            ax.set_xticks(range(len(worker_counts)),
                          [f"{n} client processes" for n in worker_counts])
            ax.legend(loc="upper center", bbox_to_anchor=(.5, -.15), ncol=3, frameon=False)
        ax.set_axisbelow(True)
        ax.grid(axis="y", alpha=.25)
        filename = metric + ".png"
        fig.savefig(args.out / filename, dpi=160); plt.close(fig); figures.append(filename)
    figures.extend(latency_histogram_plots({k: v for k, v in groups.items() if len(v) >= args.min_runs}, args.out, pooled=True))
    lines = ["# Server comparison and end-to-end latency", "", f"Workload: {settings['size']}-byte local echo; **{settings['connections']} total active connections**, divided across workers.",
             f"Server remains single-threaded, pinned to logical CPU {meta['server_cpu']}. Client CPUs: {meta['client_cpus']} (separate physical cores).",
             f"Target TOTAL requests/s: {settings.get('rate', 0)}. Zero means unpaced closed-loop traffic; nonzero means controlled load. These observations alone do not establish maximum server capacity.",
             f"Requested duration: {settings['duration']}s per worker; repetitions: {settings['runs']}. Analysis requires {args.min_runs} usable repetitions.",
             "", "| Engine | Client processes | Runs | Median requests/s | Server CPU us/request | Peak RSS KiB | Server CPU % |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for s in summaries:
        lines.append(f"| {s['engine']} | {s['workers']} | {s['runs']} | {s['throughput_requests_s_median']:.2f} | {s['server_cpu_us_per_request_median']:.3f} | {s['server_peak_rss_kib_median']:.0f} | {s['server_cpu_percent_median']:.2f} |")
    lines.extend(["", "## How to interpret", "",
        "All comparison figures use grouped bar charts, including comparisons across client-process counts. Colours identify servers; values are medians without error bars. Run minima and maxima remain in aggregates.csv for assessing variation. Latency-distribution bar charts show percentages of actual replies in common latency ranges, with a final overflow bar. All samples are included. Missing results are omitted, not plotted as zero.",
        "Increasing throughput when client processes increase, at the same total connection count, is evidence that the single-client arrangement limited observed throughput. This does not isolate Python CPU cost from scheduling/placement effects or prove maximum server capacity.",
        "A plateau alone does not prove server saturation. Compare server CPU, load-generator quality checks and variation; physical-core type, scheduler, frequency and other system work still matter.",
        "Client CPU is diagnostic, not a comparison graph. Saturation (>=85%) and missed rate targets (<95%) are reported as warnings, not automatic exclusions. End-to-end latency includes client scheduling, network and response validation; it is not isolated server latency.",
        "Throughput = all successful requests / (latest worker finish - earliest worker start), never a sum of worker rates. Server CPU snapshots bracket the common release (including a 50ms lead) through the last measured completion; raw interval and skew are retained. JSON writing and client result sorting occur afterwards.",
        "Use unpaced worker-scaling studies to investigate load-generator limits, and nonzero rate sweeps to compare resource costs and latency at controlled load. Keep settings consistent across engines. Graphs omit error bars for clarity; the recorded min/max values still need to be discussed when variation is material.",
        "Short runs (<5s per worker), fewer than 1000 total samples, or >100ms start/finish skew are excluded. Thresholds are screening heuristics, not statistical guarantees.",
        "Physical-core separation does not isolate shared cache, memory, temperature, interrupts or other OS work. Process CPU excludes unrelated client CPU but is not whole-system networking CPU.",
        "", f"Exclusions: {len(excluded)}; see exclusions.json. Raw evidence remains in the input folder.", ""])
    lines.extend(["## Load-generator warnings", ""])
    for row in rows:
        lines.extend(f"- {row['engine']} / {row['workers']} workers / run {row['run']}: {w}" for w in row.get("warnings", []))
    lines.extend(f"![{f}]({f})\n" for f in figures)
    (args.out / "report.md").write_text("\n".join(lines) + "\n")
    for f in sorted(args.input.rglob("*.json")):
        hashes[str(f.resolve())] = digest(f)
    write_json(args.out / "provenance.json", {"metadata": meta, "input_sha256": hashes, "analyzer_sha256": digest(Path(__file__))})
    print(f"Capacity report and {len(figures)} graphs: {args.out}")
    return 2 if excluded else 0


def capacity_selftest():
    """Small arithmetic fixtures: common-window pooling and rejection of corrupted evidence."""
    def fixture(start, end, latencies):
        elapsed = end - start
        ordered = sorted(latencies)
        n = len(ordered)
        return dict(valid=True, errors=[], app="echo", payload_bytes=64,
                    connections=n, active=n, attempted=n, completed=n,
                    measured_per_connection=[1] * n, requests_per_active_connection=1,
                    warmup_per_active_connection=1, duration_requested_s=5,
                    measurement_started_s=start, measurement_finished_s=end, elapsed_s=elapsed,
                    latencies_us=latencies, throughput_requests_s=n / elapsed,
                    payload_mib_s=n * 64 / 1048576 / elapsed,
                    client_measured_cpu_s=1, client_cpu_percent=100 / elapsed,
                    p50_us=ordered[math.ceil(n * .5) - 1],
                    p95_us=ordered[math.ceil(n * .95) - 1], p99_us=ordered[math.ceil(n * .99) - 1])
    clients = [fixture(1, 6, [10, 20]), fixture(1.01, 6.02, [15, 30])]
    merged = combine_capacity_clients(clients, {})
    assert merged["completed"] == 4 and merged["connections"] == 4
    assert math.isclose(merged["elapsed_s"], 5.02)
    assert math.isclose(merged["throughput_requests_s"], 4 / 5.02)
    assert not math.isclose(merged["throughput_requests_s"], sum(c["throughput_requests_s"] for c in clients))
    assert merged["p50_us"] == 15 and merged["p95_us"] == merged["p99_us"] == 30
    assert math.isclose(merged["client_cpu_percent"], 200 / 5.02)
    for field, bad in (("completed", 999), ("p95_us", 999), ("client_cpu_percent", 999),
                       ("measurement_finished_s", 99), ("valid", False),
                       ("measured_per_connection", [2, 2]), ("latencies_us", [float("nan"), 20])):
        corrupted = [dict(c) for c in clients]
        corrupted[0][field] = bad
        try:
            combine_capacity_clients(corrupted, {})
        except ValueError:
            pass
        else:
            raise AssertionError(f"Corrupted capacity field was accepted: {field}")
    before = dict(time=.95, cpu_ticks=0, voluntary=1, involuntary=2, threads=1, snapshot_span_s=.001)
    after = dict(time=6.1, cpu_ticks=2 * os.sysconf("SC_CLK_TCK"), voluntary=11,
                 involuntary=4, threads=1, snapshot_span_s=.001)
    resources = resource_difference(before, after)
    server = dict(engine="select", app="echo", server_ok=True, accepted=4, closed=4,
                  peak=4, errors=0, rejected=0, incomplete=0, messages=8,
                  wire_bytes_read=8 * 72, wire_bytes_written=8 * 64, max_rss_kib=1000)
    result = capacity_metrics(clients, resources, server, 0, "select")
    assert result["server_cpu_us_per_request"] == 500000
    for bad_server, exit_code in ((dict(server, messages=7), 0), (server, 1)):
        try:
            capacity_metrics(clients, resources, bad_server, exit_code, "select")
        except ValueError:
            pass
        else:
            raise AssertionError("Failed server/accounting accepted by capacity validation")


# ----- Command selection -----


def main():
    commands = {"build": build_main, "client": client_main, "test": test_main,
                "benchmark": benchmark_main, "profile": profile_main,
                "analyze": analyze_main, "selftest": selftest_main,
                "capacity": capacity_main, "capacity-analyze": capacity_analyze_main}
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0 if len(sys.argv) > 1 else 2
    command = sys.argv[1]
    if command not in commands:
        print(f"Unknown command: {command}. Use --help for available commands.", file=sys.stderr)
        return 2
    if command in ("test", "selftest") and not __debug__:
        print("Run tests without -O: assertions must remain enabled.", file=sys.stderr)
        return 2
    try:
        return commands[command](sys.argv[2:])
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"{command}: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
