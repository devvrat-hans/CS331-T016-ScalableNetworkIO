import matplotlib.pyplot as plt
import numpy as np

# 1. Latency Percentiles (from your latency_profiler.py runs)
percentiles = ['p50', 'p90', 'p95', 'p99', 'Max']
select = [10.49, 21.28, 27.95, 31.59, 33.12]
poll = [10.68, 24.96, 29.70, 33.93, 36.00]
epoll = [8.99, 24.92, 28.25, 37.58, 38.79]
io_uring = [7.81, 23.09, 27.60, 30.06, 30.96]

x = np.arange(len(percentiles))
width = 0.2

fig, ax = plt.subplots(figsize=(8, 5))
ax.bar(x - 1.5*width, select, width, label='select')
ax.bar(x - 0.5*width, poll, width, label='poll')
ax.bar(x + 0.5*width, epoll, width, label='epoll')
ax.bar(x + 1.5*width, io_uring, width, label='io_uring')
ax.set_ylabel('Latency (ms)')
ax.set_title('Latency Percentiles at 1,000 Concurrent Connections')
ax.set_xticks(x)
ax.set_xticklabels(percentiles)
ax.legend()
plt.tight_layout()
plt.savefig('latency_cdf.png')
plt.close()

# 2. Normalized Syscalls per Message
engines = ['select', 'poll', 'epoll', 'io_uring']
syscalls = [2.256, 2.254, 2.382, 0.101]

fig, ax = plt.subplots(figsize=(6, 4))
ax.bar(engines, syscalls, color=['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728'])
ax.set_ylabel('Syscalls per Message')
ax.set_title('System Call Overhead (Normalized)')
plt.tight_layout()
plt.savefig('syscalls.png')
plt.close()

# 3. Real Measured CPU Utilization (from /usr/bin/time -v logs)
conns = ['10', '100', '1000']
cpu_select = [80, 82, 80]
cpu_poll   = [80, 86, 88]
cpu_epoll  = [82, 81, 83]
cpu_uring  = [84, 89, 90]

fig, ax = plt.subplots(figsize=(7, 4.5))
ax.plot(conns, cpu_select, marker='o', label='select')
ax.plot(conns, cpu_poll,   marker='v', label='poll')
ax.plot(conns, cpu_epoll,  marker='^', label='epoll')
ax.plot(conns, cpu_uring,  marker='s', label='io_uring')

ax.set_xlabel('Concurrent Connections')
ax.set_ylabel('CPU Utilization (%)')
ax.set_title('Measured CPU Utilization Across Concurrency Levels')
ax.set_ylim(70, 100)
ax.legend()
plt.tight_layout()
plt.savefig('cpu_mem.png')
plt.close()
