# Modbus RTU/TCP Gateway

A lightweight software gateway that bridges Modbus RTU (serial) and Modbus TCP (network) communication. Designed for embedded Linux platforms such as Raspberry Pi, BeagleBone, and similar SBCs.

---

## Why This Matters

Many industrial and maritime systems use Modbus RTU over RS-232/RS-485 for sensor and actuator control. Modern SCADA, monitoring, and cloud systems communicate via Modbus TCP over Ethernet. This gateway solves the translation problem between the two worlds - no proprietary hardware required.

Real use cases:
- Ship engine room sensor monitoring
- Industrial PLC integration with modern dashboards
- Remote telemetry systems over LAN/WAN
- Retrofit of legacy RS-485 equipment to Ethernet networks

---

## Project Structure

```
modbus-rtu-tcp-gateway/
├── src/
│   ├── gateway.c          # Core gateway logic (C)
│   ├── modbus_rtu.c       # RTU framing, CRC16, serial I/O
│   ├── modbus_rtu.h
│   ├── modbus_tcp.c       # TCP server, MBAP header handling
│   ├── modbus_tcp.h
│   └── config.h           # Compile-time configuration
├── scripts/
│   ├── monitor.py         # Real-time register monitor (Python)
│   └── simulate_rtu.py    # RTU slave simulator for testing
├── tests/
│   ├── test_crc.c         # CRC16 unit tests
│   └── test_framing.c     # Frame parsing tests
├── docs/
│   └── protocol_notes.md  # Modbus protocol reference notes
├── Makefile
└── README.md
```

---

## Requirements

### Hardware (for real deployment)
- Embedded Linux board (Raspberry Pi 3/4, BeagleBone, etc.)
- USB-to-RS485 adapter or onboard UART with RS-485 transceiver
- Modbus RTU slave device (PLC, sensor, actuator)

### Software
- GCC >= 9.0
- POSIX-compliant Linux (tested on Raspberry Pi OS, Ubuntu 22.04)
- Python >= 3.8 (for monitor and simulator scripts)
- Python packages: `pip install pymodbus pyserial matplotlib`

---

## Build

```bash
# Clone and build
git clone <repo-url>
cd modbus-rtu-tcp-gateway
make

# Build with debug output
make DEBUG=1

# Build and run tests
make test

# Clean
make clean
```

---

## Usage

### Start the Gateway

```bash
# Basic usage (serial port, baud rate, TCP port)
./build/gateway --rtu /dev/ttyUSB0 --baud 9600 --tcp-port 502

# With verbose logging
./build/gateway --rtu /dev/ttyUSB0 --baud 9600 --tcp-port 502 --verbose

# Example with common RS-485 settings
./build/gateway --rtu /dev/ttyUSB0 --baud 19200 --parity N --stop 1 --tcp-port 502
```

### Simulate an RTU Slave (for testing without hardware)

```bash
# Start a virtual Modbus RTU slave on a pseudo-terminal
python3 scripts/simulate_rtu.py --port /dev/ttyUSB1 --slave-id 1
```

### Real-Time Register Monitor

```bash
# Connect to gateway and display register values live
python3 scripts/monitor.py --host 127.0.0.1 --port 502 --slave 1

# Monitor specific register range
python3 scripts/monitor.py --host 127.0.0.1 --port 502 --slave 1 --start 0 --count 10

# Save log to CSV
python3 scripts/monitor.py --host 127.0.0.1 --port 502 --slave 1 --log data.csv
```

---

## Configuration

Edit `src/config.h` before building to set compile-time defaults:

```c
#define DEFAULT_TCP_PORT     502
#define DEFAULT_BAUD_RATE    9600
#define RTU_TIMEOUT_MS       500     // Wait for RTU response
#define MAX_TCP_CLIENTS      8       // Concurrent TCP clients
#define LOG_LEVEL            LOG_INFO
```

---

## Supported Modbus Function Codes

| Code | Name | Supported |
|------|------|-----------|
| 0x01 | Read Coils | Yes |
| 0x02 | Read Discrete Inputs | Yes |
| 0x03 | Read Holding Registers | Yes |
| 0x04 | Read Input Registers | Yes |
| 0x05 | Write Single Coil | Yes |
| 0x06 | Write Single Register | Yes |
| 0x0F | Write Multiple Coils | Yes |
| 0x10 | Write Multiple Registers | Yes |

---

## Testing

```bash
# Run unit tests
make test

# Run with valgrind (memory check)
make memcheck

# Run integration test (requires simulate_rtu.py running)
python3 tests/integration_test.py
```

---

## Protocol Reference

See `docs/protocol_notes.md` for detailed notes on:
- Modbus RTU frame structure and CRC16 calculation
- MBAP header format for Modbus TCP
- Timing requirements and inter-frame delays
- Error handling and exception codes

---

## License

MIT License - see LICENSE file for details.
