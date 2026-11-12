# Sourced by run_all.sh: tools and machine settings.
problems=0
for tool in cc make pkg-config strace python3; do
    command -v "$tool" >/dev/null || { echo "Missing $tool. Run: sudo apt install build-essential pkg-config liburing-dev strace python3 python3-matplotlib"; exit 1; }
done
pkg-config --exists liburing || { echo "Missing liburing. Run: sudo apt install liburing-dev"; exit 1; }
command -v ss >/dev/null || { echo "Missing ss. Run: sudo apt install iproute2"; exit 1; }
python3 -c 'import matplotlib' || { echo "Missing matplotlib. Run: sudo apt install python3-matplotlib"; exit 1; }

# The first run showed two speeds on the same code when the CPU could change
# frequency, and UDP losses from the default socket-buffer limit. Check both.
if grep -qv performance /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null; then
    echo "CPU governor is not 'performance'. Run:"
    echo "  echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
    problems=1
fi
for supply in /sys/class/power_supply/*; do
    if [ "$(cat "$supply/type" 2>/dev/null)" = "Mains" ] && [ "$(cat "$supply/online")" != "1" ]; then
        echo "Laptop is on battery. Plug it in."
        problems=1
    fi
done
if [ "$(cat /proc/sys/net/core/rmem_max)" -lt 8388608 ] || [ "$(cat /proc/sys/net/core/wmem_max)" -lt 8388608 ]; then
    echo "UDP socket buffers are limited. Run:"
    echo "  sudo sysctl -w net.core.rmem_max=8388608 net.core.wmem_max=8388608"
    problems=1
fi
if [ "$problems" = 1 ] && [ "${FORCE:-0}" != 1 ]; then
    echo "Fix the items above and run again (FORCE=1 runs anyway; the settings are recorded either way)."
    exit 1
fi

