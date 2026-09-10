import asyncio, time, sys

async def echo_client(port, messages, latencies):
    try:
        reader, writer = await asyncio.open_connection('127.0.0.1', port)
        for _ in range(messages):
            start = time.perf_counter()
            writer.write(b'PING')
            await writer.drain()
            await reader.read(4)
            latencies.append((time.perf_counter() - start) * 1_000_000)
        writer.close()
    except Exception:
        pass

async def main():
    port, conns, msgs = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    latencies = []
    await asyncio.gather(*(echo_client(port, msgs, latencies) for _ in range(conns)))
    
    if not latencies: return print("Error: No data")
    latencies.sort()
    n = len(latencies)
    print(f"p50: {latencies[int(n*0.50)]:.1f} µs")
    print(f"p90: {latencies[int(n*0.90)]:.1f} µs")
    print(f"p95: {latencies[int(n*0.95)]:.1f} µs")
    print(f"p99: {latencies[int(n*0.99)]:.1f} µs")
    print(f"Max: {latencies[-1]:.1f} µs")

asyncio.run(main())
