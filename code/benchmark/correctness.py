#!/usr/bin/env python3
"""Correctness tests for every engine, variant, thread count and transport.

The nine TCP cases are the ones that verified the earlier version. They run
against each configuration here; UDP and the new plugins add their own cases.
Correctness comes first: a benchmark of a server that loses data means nothing.
"""
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import testing_v4_reference as core  # noqa: E402

CONFIGS = [("select", []), ("poll", []), ("epoll", []), ("epoll", ["--epoll-mode", "et"]),
           ("io_uring", []), ("io_uring", ["--sqpoll", "1"]),
           ("select", ["--threads", "2"]), ("poll", ["--threads", "2"]),
           ("epoll", ["--threads", "2"]), ("io_uring", ["--threads", "2"]),
           ("io_uring", ["--udp-depth", "1"]),
           ("epoll", ["--epoll-mode", "lt-direct"]), ("io_uring", ["--sqpoll", "1", "--sqpoll-busy", "1"]),
           ("select", ["--buffer", "65536"]), ("io_uring", ["--buffer", "65536"]),
           ("select", ["--send-mode", "full"]), ("poll", ["--send-mode", "full"]),
           ("epoll", ["--send-mode", "full"]), ("epoll", ["--epoll-mode", "lt-direct", "--send-mode", "full"])]
UDP_CONFIGS = CONFIGS[:6] + CONFIGS[10:13]
TCP_CASES = [("echo", core.test_echo),
             ("upload", lambda e, p: core.test_bulk(e, "upload", p)),
             ("download", lambda e, p: core.test_bulk(e, "download", p)),
             ("errors_shutdown", core.test_errors_and_shutdown),
             ("limit", core.test_limit),
             ("many_connections", core.test_many_connections),
             ("invalid_requests", core.test_invalid_requests),
             ("reset_during_send", core.test_reset_during_send),
             ("signal_options", core.test_signal_and_options)]


def start(engine, extra, app, folder, transport="tcp"):
    folder.mkdir(parents=True, exist_ok=True)
    out = folder / "server.stdout"
    with out.open("wb") as handle, (folder / "server.stderr").open("wb") as err:
        process = subprocess.Popen([str(core.ROOT / "bin" / f"{engine}_server"), "--port", "0", "--app", app,
                                    "--transport", transport, *extra], stdout=handle, stderr=err)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        for line in out.read_text().splitlines():
            if line.startswith("READY "):
                return process, int(line.split()[1]), out
        time.sleep(0.01)
    process.kill()
    raise RuntimeError("no READY: " + (folder / "server.stderr").read_text())


def finish(process, out):
    process.terminate()
    process.wait(timeout=8)
    assert process.returncode == 0, f"exit {process.returncode}"
    return [json.loads(line) for line in out.read_text().splitlines() if line.startswith("{")][0]


def exchange(sock, seq, reply, body=b""):
    sock.send(struct.pack("!QQ", seq, reply) + body)
    return sock.recv(70000)


def test_udp(engine, extra, folder):
    pattern = core.payload
    # Sizes straddle the Ethernet MTU, so both unfragmented and fragmented
    # datagrams are covered (loopback's own MTU is larger, 65536).
    process, port, out = start(engine, extra, "echo", folder / "echo", "udp")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(3)
        s.connect(("127.0.0.1", port))
        for seq, size in enumerate((0, 1, 64, 1400, 1472, 1473, 8000, 60000), 1):
            body = bytes(range(256)) * (size // 256 + 1)
            reply = exchange(s, seq, 0, body[:size])
            assert reply[:8] == struct.pack("!Q", seq) and reply[16:] == body[:size], size
        s.send(b"short")   # Malformed: must be dropped, not crash the server.
        assert exchange(s, 99, 0, b"ok")[16:] == b"ok"
    summary = finish(process, out)
    assert summary["messages"] == 9 and summary["errors"] == 1, summary

    process, port, out = start(engine, extra, "download", folder / "download", "udp")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(3)
        s.connect(("127.0.0.1", port))
        for size in (1, 1400, 30000):
            assert exchange(s, 1, size)[16:] == pattern(size), size
    finish(process, out)

    process, port, out = start(engine, extra, "upload", folder / "upload", "udp")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(3)
        s.connect(("127.0.0.1", port))
        assert len(exchange(s, 1, 0, pattern(4000))) == 16
        s.settimeout(0.3)
        bad = bytearray(pattern(100))
        bad[50] ^= 0xff
        try:
            exchange(s, 2, 0, bytes(bad))
            raise AssertionError("corrupt datagram was acknowledged")
        except socket.timeout:
            pass
    summary = finish(process, out)
    assert summary["messages"] == 1 and summary["errors"] == 1, summary


def test_plugins(engine, extra, folder):
    process, port, out = start(engine, extra, "noop", folder / "noop")
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.sendall(struct.pack("!Q", 1))
        assert core.recv_exact(s, 1) == b"K"
    assert finish(process, out)["messages"] == 1

    process, port, out = start(engine, extra, "memory", folder / "memory")
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.sendall(struct.pack("!Q", 10000) + b"m" * 10000)
        assert core.recv_exact(s, 1) == b"K"
    summary = finish(process, out)
    assert summary["messages"] == 1 and summary["max_rss_kib"] > 256, summary

    # Requests are written one after another and wrap around the 256 KiB buffer.
    process, port, out = start(engine, extra, "memory", folder / "memory-wrap")
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        for size in (100000, 100000, 100000, 262144, 4096):
            s.sendall(struct.pack("!Q", size) + b"w" * size)
            assert core.recv_exact(s, 1) == b"K", size
    summary = finish(process, out)
    assert summary["messages"] == 5 and summary["errors"] == 0, summary

    code = subprocess.run([str(core.ROOT / "bin" / f"{engine}_server"), "--app", "memory", "--transport", "udp"],
                          capture_output=True, timeout=5).returncode
    assert code == 2, "memory has no datagram mode and must be refused"


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="v5-tests-"))
    only = sys.argv[2] if len(sys.argv) > 2 else None
    outcomes = []

    def record(label, name, function):
        if only and only not in label:
            return
        try:
            function()
            outcome = dict(config=label, test=name, passed=True)
        except Exception as exc:
            outcome = dict(config=label, test=name, passed=False, error=f"{type(exc).__name__}: {exc}"[:400])
        outcomes.append(outcome)
        print(json.dumps(outcome), flush=True)

    for engine, extra in CONFIGS:
        label = engine + "".join("_" + x.strip("-") for x in extra)
        core.EXTRA_ARGS = extra
        for name, function in TCP_CASES:
            record(label, name, lambda: function(engine, folder / label / name))
        core.EXTRA_ARGS = []
        record(label, "plugins", lambda: test_plugins(engine, extra, folder / label / "plugins"))
        if (engine, extra) in UDP_CONFIGS:
            record(label, "udp", lambda: test_udp(engine, extra, folder / label / "udp"))
    (folder / "tests.json").write_text(json.dumps(outcomes, indent=2))
    failed = [o for o in outcomes if not o["passed"]]
    print(f"{len(outcomes) - len(failed)}/{len(outcomes)} passed. Logs: {folder}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
