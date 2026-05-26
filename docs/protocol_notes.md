# Modbus Protocol Reference Notes

## Modbus RTU Frame Structure

```
[ Slave ID ][ Function Code ][ Data ... ][ CRC Low ][ CRC High ]
    1 byte       1 byte        0-252 bytes   1 byte     1 byte
```

- **Slave ID**: 1-247 (0 = broadcast, 248-255 reserved)
- **CRC**: CRC16 with polynomial 0xA001, initial value 0xFFFF
- **Inter-frame gap**: minimum 3.5 character times of silence

### CRC16 Calculation

```
CRC16(data):
    crc = 0xFFFF
    for each byte b in data:
        crc ^= b
        for i in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc
```

CRC is appended **low byte first**.

---

## Modbus TCP Frame Structure (MBAP + PDU)

```
[ Transaction ID ][ Protocol ID ][ Length ][ Unit ID ][ Function Code ][ Data ]
      2 bytes          2 bytes     2 bytes    1 byte       1 byte        N bytes
```

- **Transaction ID**: Echoed back in response (for request matching)
- **Protocol ID**: Always 0x0000 for Modbus
- **Length**: Number of bytes following (Unit ID + PDU)
- **Unit ID**: Slave address (same as RTU Slave ID)
- **No CRC** in TCP - TCP/IP handles error checking

---

## RTU <-> TCP Conversion

### TCP to RTU:
1. Extract Unit ID from MBAP header
2. Extract PDU (Function Code + Data)
3. Build RTU frame: `[Unit ID][PDU][CRC16]`

### RTU to TCP:
1. Remove RTU Slave ID and CRC (last 2 bytes)
2. Build MBAP header with original Transaction ID and Unit ID
3. Assemble: `[MBAP Header][PDU]`

---

## Supported Function Codes

| FC   | Name                    | Request Format                    | Response Format                   |
|------|-------------------------|-----------------------------------|-----------------------------------|
| 0x01 | Read Coils              | [addr 2B][count 2B]               | [byte_count 1B][data NB]          |
| 0x02 | Read Discrete Inputs    | [addr 2B][count 2B]               | [byte_count 1B][data NB]          |
| 0x03 | Read Holding Registers  | [addr 2B][count 2B]               | [byte_count 1B][regs 2*N B]       |
| 0x04 | Read Input Registers    | [addr 2B][count 2B]               | [byte_count 1B][regs 2*N B]       |
| 0x05 | Write Single Coil       | [addr 2B][0xFF00 or 0x0000]       | Echo of request                   |
| 0x06 | Write Single Register   | [addr 2B][value 2B]               | Echo of request                   |
| 0x0F | Write Multiple Coils    | [addr 2B][count 2B][bc 1B][data]  | [addr 2B][count 2B]               |
| 0x10 | Write Multiple Registers| [addr 2B][count 2B][bc 1B][data]  | [addr 2B][count 2B]               |

---

## Exception Response

If slave cannot process a request:
```
[ Slave ID ][ FC | 0x80 ][ Exception Code ][ CRC ]
```

Exception codes:
- `0x01` - Illegal Function
- `0x02` - Illegal Data Address
- `0x03` - Illegal Data Value
- `0x04` - Server Device Failure
- `0x0A` - Gateway Path Unavailable
- `0x0B` - Gateway Target Device Failed to Respond

---

## Timing Requirements (RTU)

At 9600 baud, 1 character = 11 bits = 1.146 ms

- **Inter-frame gap** (silence before new frame): >= 3.5 chars = ~4 ms
- **Inter-character gap** (max inside a frame): <= 1.5 chars = ~1.7 ms
- Frames with gaps > 1.5 chars are considered corrupted

These gaps are managed automatically by the serial port driver when using
`tcgetattr`/`tcsetattr` with appropriate `VMIN`/`VTIME` settings.

---

## RS-485 Half-Duplex Considerations

RS-485 is half-duplex: master must disable its transmitter before listening.

With USB-RS485 adapters, this is handled automatically by the adapter hardware.

With native UART + transceiver:
- Use RTS pin to control transmit enable
- Linux: `TIOCSRS485` ioctl or `rs485_enable` device tree property
