#!/usr/bin/env python3
"""
monitor.py - Real-time Modbus TCP register monitor

Connects to the gateway and displays register values in a live-updating
terminal dashboard. Optionally logs to CSV.

Usage:
    python3 scripts/monitor.py --host 127.0.0.1 --port 502 --slave 1
    python3 scripts/monitor.py --host 192.168.1.10 --port 502 --slave 1 --start 0 --count 10 --log data.csv
"""

import argparse
import csv
import sys
import time
import signal
from datetime import datetime

try:
    from pymodbus.client import ModbusTcpClient
except ImportError:
    print("ERROR: pymodbus not installed. Run: pip install pymodbus")
    sys.exit(1)

# ── Config ────────────────────────────────────────────────
POLL_INTERVAL_S = 1.0
DISPLAY_WIDTH   = 60

# ── Signal handler ────────────────────────────────────────
running = True
def _sig(sig, frame):
    global running
    running = False
signal.signal(signal.SIGINT, _sig)
signal.signal(signal.SIGTERM, _sig)

# ── Terminal helpers ──────────────────────────────────────
def clear_line():
    print("\033[K", end="")

def move_up(n):
    print(f"\033[{n}A", end="")

def print_header(host, port, slave_id, reg_start, reg_count):
    print("=" * DISPLAY_WIDTH)
    print(f"  Modbus RTU/TCP Gateway - Register Monitor")
    print(f"  Host: {host}:{port}  |  Slave: {slave_id}")
    print(f"  Registers: {reg_start} to {reg_start + reg_count - 1}")
    print("=" * DISPLAY_WIDTH)
    print(f"  {'Timestamp':<22} {'Reg':>6}  {'Value':>8}  {'Hex':>7}")
    print("-" * DISPLAY_WIDTH)

def print_registers(timestamp, registers, reg_start):
    for i, val in enumerate(registers):
        reg_addr = reg_start + i
        bar_len  = min(20, val * 20 // 65535) if val > 0 else 0
        bar      = "#" * bar_len + "." * (20 - bar_len)
        print(f"  {timestamp:<22} {reg_addr:>6}  {val:>8}  {val:#07x}  [{bar}]",
              end="\n")

# ── Main polling loop ─────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Modbus TCP Register Monitor")
    parser.add_argument("--host",  default="127.0.0.1", help="Gateway host")
    parser.add_argument("--port",  type=int, default=502, help="Gateway TCP port")
    parser.add_argument("--slave", type=int, default=1,   help="Modbus slave ID")
    parser.add_argument("--start", type=int, default=0,   help="Start register address")
    parser.add_argument("--count", type=int, default=8,   help="Number of registers to read")
    parser.add_argument("--interval", type=float, default=POLL_INTERVAL_S, help="Poll interval (seconds)")
    parser.add_argument("--log",   default=None, help="CSV log file path")
    args = parser.parse_args()

    client = ModbusTcpClient(args.host, port=args.port)
    if not client.connect():
        print(f"ERROR: Cannot connect to {args.host}:{args.port}")
        sys.exit(1)

    print(f"\033[?25l", end="")  # hide cursor
    print_header(args.host, args.port, args.slave, args.start, args.count)

    csv_writer = None
    csv_file   = None
    if args.log:
        csv_file   = open(args.log, "w", newline="")
        csv_writer = csv.writer(csv_file)
        header_row = ["timestamp"] + [f"reg_{args.start + i}" for i in range(args.count)]
        csv_writer.writerow(header_row)
        print(f"  Logging to: {args.log}")

    first = True
    lines_per_update = args.count + 2

    try:
        while running:
            ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:23]

            result = client.read_holding_registers(
                address=args.start,
                count=args.count,
                slave=args.slave
            )

            if result.isError():
                values = ["ERR"] * args.count
                display_vals = [0] * args.count
            else:
                values       = result.registers
                display_vals = values

            if not first:
                move_up(lines_per_update)

            print(f"  Last poll: {ts}  {'[OK]' if not result.isError() else '[ERROR]'}")
            print("-" * DISPLAY_WIDTH)
            print_registers(ts, display_vals, args.start)
            first = False

            if csv_writer and not result.isError():
                csv_writer.writerow([ts] + list(values))
                csv_file.flush()

            time.sleep(args.interval)

    finally:
        print("\033[?25h")  # show cursor
        client.close()
        if csv_file:
            csv_file.close()
            print(f"\nLog saved to: {args.log}")
        print("\nMonitor stopped.")


if __name__ == "__main__":
    main()
