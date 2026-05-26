#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

/**
 * modbus_rtu.h - Modbus RTU framing, CRC16, and serial I/O
 */

#include <stdint.h>
#include <stddef.h>
#include "config.h"

/* Modbus RTU max frame: 256 bytes (1 slave + 1 FC + 252 data + 2 CRC) */
#define MODBUS_RTU_MAX_FRAME 256

/* Opaque RTU context */
typedef struct rtu_ctx rtu_ctx_t;

/**
 * rtu_open - Open and configure a serial port for Modbus RTU
 * @device:    Path to serial device (e.g. "/dev/ttyUSB0")
 * @baud_rate: Baud rate (9600, 19200, 38400, 115200, ...)
 * @parity:    'N' = none, 'E' = even, 'O' = odd
 * @stop_bits: 1 or 2
 * Returns: pointer to RTU context, or NULL on failure
 */
rtu_ctx_t *rtu_open(const char *device, int baud_rate,
                    char parity, int stop_bits);

/**
 * rtu_close - Close serial port and free context
 */
void rtu_close(rtu_ctx_t *ctx);

/**
 * rtu_transaction - Send RTU request and receive response
 * @ctx:         RTU context
 * @request:     Complete RTU frame (slave_id + PDU + CRC)
 * @req_len:     Length of request frame
 * @response:    Buffer for response frame
 * @resp_size:   Size of response buffer
 * @timeout_ms:  Maximum wait time for response in milliseconds
 * Returns: length of received response, or -1 on error/timeout
 */
int rtu_transaction(rtu_ctx_t *ctx,
                    const uint8_t *request, int req_len,
                    uint8_t *response, int resp_size,
                    int timeout_ms);

/**
 * rtu_crc16 - Calculate CRC16 for Modbus RTU
 * @data: Input bytes
 * @len:  Number of bytes
 * Returns: CRC16 value (little-endian when appended to frame)
 */
uint16_t rtu_crc16(const uint8_t *data, size_t len);

/**
 * rtu_build_frame - Build a complete RTU frame from slave ID and PDU
 * Appends CRC automatically.
 * @slave_id:  Modbus slave address (1-247)
 * @pdu:       PDU bytes (function code + data)
 * @pdu_len:   PDU length
 * @frame:     Output buffer (must be >= pdu_len + 3)
 * Returns: total frame length
 */
int rtu_build_frame(uint8_t slave_id,
                    const uint8_t *pdu, int pdu_len,
                    uint8_t *frame);

/**
 * rtu_validate_frame - Check slave ID, length, and CRC of received frame
 * Returns: 0 on success, -1 on invalid frame
 */
int rtu_validate_frame(const uint8_t *frame, int len);

#endif /* MODBUS_RTU_H */
