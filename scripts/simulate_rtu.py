#!/usr/bin/env python3
"""
simulate_rtu.py - Modbus RTU Slave Simulator

Simulates a Modbus RTU slave device for testing the gateway without
physical hardware. Runs on a serial port (real or virtual pty).

Supported function codes:
  FC01 - Read Coils
  FC02 - Read Discrete Inputs
  FC03 - Read Holding Registers
  FC04 - Read Input Registers
  FC05 - Write Single Coil
  FC06 - Write Single Register

Usage:
    # On real serial port
    python3 scripts/simulate_rtu.py --port /dev/ttyUSB1 --slave-id 1

    # On virtual pseudo-terminal (for local testing, Linux/macOS)
    python3 scripts/simulate_rtu.py --pty --slave-id 1
"""

import argparse
import struct
import sys
import time
import signal
import random
import os
import threading

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

# ── State ─────────────────────────────────────────────────
class SlaveState:
    def __init__(self):
        self.holding_regs    = [0] * 256
        self.input_regs      = [int(random.uniform(100, 60000)) for _ in range(256)]
        self.coils           = [False] * 256
        self.discrete_inputs = [bool(random.randint(0, 1)) for _ in range(256)]

        # Simulate some "sensor" values that drift over time
        self.holding_regs[0] = 2500   # Temperature * 10 (25.0 C)
        self.holding_regs[1] = 1013   # Pressure hPa
        self.holding_regs[2] = 600    # Speed RPM / 10
        self.holding_regs[3] = 0      # Status flags

    def tick(self):
        """Simulate slowly changing sensor values"""
        self.holding_regs[0] = max(0, min(65535, self.holding_regs[0] + random.randint(-5, 5)))
        self.holding_regs[1] = max(900, min(1100, self.holding_regs[1] + random.randint(-2, 2)))
        self.holding_regs[2] = max(0,   min(6000, self.holding_regs[2] + random.randint(-10, 10)))
        self.input_regs[0]   = int(random.uniform(0, 65535))

# ── CRC16 ─────────────────────────────────────────────────
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def append_crc(data: bytes) -> bytes:
    c = crc16(data)
    return data + bytes([c & 0xFF, (c >> 8) & 0xFF])

def check_crc(data: bytes) -> bool:
    if len(data) < 3:
        return False
    payload = data[:-2]
    expected = crc16(payload)
    received = data[-2] | (data[-1] << 8)
    return expected == received

# ── Response builders ─────────────────────────────────────
def pack_bits(bits, count):
    byte_count = (count + 7) // 8
    data = bytearray(byte_count)
    for i in range(count):
        if bits[i]:
            data[i // 8] |= (1 << (i % 8))
    return bytes(data)

def build_response(slave_id, fc, data: bytes) -> bytes:
    frame = bytes([slave_id, fc]) + data
    return append_crc(frame)

def build_exception(slave_id, fc, ex_code) -> bytes:
    frame = bytes([slave_id, fc | 0x80, ex_code])
    return append_crc(frame)

# ── Process request ───────────────────────────────────────
def process_request(state: SlaveState, slave_id: int, frame: bytes):
    if len(frame) < 6:
        return None
    req_slave = frame[0]
    if req_slave != slave_id:
        return None  # Not for us
    fc      = frame[1]
    addr    = struct.unpack(">H", frame[2:4])[0]
    count   = struct.unpack(">H", frame[4:6])[0]

    if fc == 0x01:  # Read Coils
        if count < 1 or count > 2000: return build_exception(slave_id, fc, 0x03)
        bits = pack_bits(state.coils[addr:addr+count], count)
        return build_response(slave_id, fc, bytes([len(bits)]) + bits)

    elif fc == 0x02:  # Read Discrete Inputs
        if count < 1 or count > 2000: return build_exception(slave_id, fc, 0x03)
        bits = pack_bits(state.discrete_inputs[addr:addr+count], count)
        return build_response(slave_id, fc, bytes([len(bits)]) + bits)

    elif fc == 0x03:  # Read Holding Registers
        if count < 1 or count > 125: return build_exception(slave_id, fc, 0x03)
        regs = state.holding_regs[addr:addr+count]
        data = bytes([count * 2]) + struct.pack(f">{count}H", *regs)
        return build_response(slave_id, fc, data)

    elif fc == 0x04:  # Read Input Registers
        if count < 1 or count > 125: return build_exception(slave_id, fc, 0x03)
        regs = state.input_regs[addr:addr+count]
        data = bytes([count * 2]) + struct.pack(f">{count}H", *regs)
        return build_response(slave_id, fc, data)

    elif fc == 0x05:  # Write Single Coil
        value = struct.unpack(">H", frame[4:6])[0]
        state.coils[addr] = (value == 0xFF00)
        return build_response(slave_id, fc, frame[2:6])

    elif fc == 0x06:  # Write Single Register
        value = struct.unpack(">H", frame[4:6])[0]
        state.holding_regs[addr] = value
        return build_response(slave_id, fc, frame[2:6])

    else:
        return build_exception(slave_id, fc, 0x01)  # Illegal function

# ── Serial loop ───────────────────────────────────────────
def run_slave(ser: serial.Serial, slave_id: int):
    state   = SlaveState()
    running = True

    def ticker():
        while running:
            state.tick()
            time.sleep(1.0)

    t = threading.Thread(target=ticker, daemon=True)
    t.start()

    print(f"[simulator] Slave ID={slave_id} running on {ser.port} @ {ser.baudrate} baud")
    print("[simulator] Press Ctrl+C to stop.")

    buf = b""
    while running:
        try:
            chunk = ser.read(256)
        except serial.SerialException:
            break
        if not chunk:
            continue

        buf += chunk

        # Simple framing: wait for silence gap then process
        if len(buf) >= 8:
            time.sleep(0.005)  # inter-frame gap
            if check_crc(buf):
                response = process_request(state, slave_id, buf)
                if response:
                    time.sleep(0.002)  # turnaround delay
                    ser.write(response)
                    print(f"[simulator] FC=0x{buf[1]:02X} addr={struct.unpack('>H', buf[2:4])[0]} -> {len(response)} bytes")
                buf = b""
            elif len(buf) > 256:
                print(f"[simulator] Garbage data ({len(buf)} bytes), flushing")
                buf = b""

    print("[simulator] Stopped.")

# ── Main ──────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Modbus RTU Slave Simulator")
    parser.add_argument("--port",     default="/dev/ttyUSB1", help="Serial port")
    parser.add_argument("--baud",     type=int, default=9600, help="Baud rate")
    parser.add_argument("--slave-id", type=int, default=1,    help="Modbus slave ID")
    args = parser.parse_args()

    ser = serial.Serial(
        port=args.port,
        baudrate=args.baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1
    )

    def stop(sig, frame):
        print("\n[simulator] Stopping...")
        ser.close()
        sys.exit(0)

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    run_slave(ser, args.slave_id)

if __name__ == "__main__":
    main()
