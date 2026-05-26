/**
 * modbus_rtu.c - Modbus RTU implementation
 *
 * Handles: CRC16 calculation, frame building/validation,
 * serial port configuration, and RTU transactions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>

#include "modbus_rtu.h"

/* ── Internal types ──────────────────────────────────── */
struct rtu_ctx {
    int fd;         /* serial file descriptor */
    int baud_rate;
    char parity;
    int stop_bits;
};

/* ── CRC16 lookup table (standard Modbus polynomial 0xA001) ── */
static const uint16_t crc_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0x7400, 0xB4C1, 0xB581, 0x7540, 0xB701, 0x77C0, 0x7680, 0xB641,
    0xB201, 0x72C0, 0x7380, 0xB341, 0x7100, 0xB1C1, 0xB081, 0x7040,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
};

/* ── CRC16 calculation ───────────────────────────────── */
uint16_t rtu_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)(crc ^ data[i]);
        crc = (crc >> 8) ^ crc_table[idx];
    }
    return crc;
}

/* ── Map integer baud rate to termios constant ───────── */
static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:
            fprintf(stderr, "[rtu] Unsupported baud rate %d, using 9600\n", baud);
            return B9600;
    }
}

/* ── Open and configure serial port ─────────────────── */
rtu_ctx_t *rtu_open(const char *device, int baud_rate,
                    char parity, int stop_bits) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("[rtu] open");
        return NULL;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        perror("[rtu] tcgetattr");
        close(fd);
        return NULL;
    }

    speed_t spd = baud_to_speed(baud_rate);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    /* 8 data bits */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Parity */
    tty.c_cflag &= ~(PARENB | PARODD);
    if (parity == 'E') {
        tty.c_cflag |= PARENB;
    } else if (parity == 'O') {
        tty.c_cflag |= (PARENB | PARODD);
    }

    /* Stop bits */
    if (stop_bits == 2)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    /* No hardware flow control */
    tty.c_cflag &= ~CRTSCTS;

    /* Raw mode */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_lflag  = 0;
    tty.c_oflag  = 0;

    /* Non-blocking read with timeout */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5; /* 0.5 second inter-byte timeout */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("[rtu] tcsetattr");
        close(fd);
        return NULL;
    }

    rtu_ctx_t *ctx = malloc(sizeof(rtu_ctx_t));
    if (!ctx) { close(fd); return NULL; }
    ctx->fd        = fd;
    ctx->baud_rate = baud_rate;
    ctx->parity    = parity;
    ctx->stop_bits = stop_bits;
    return ctx;
}

/* ── Close serial port ───────────────────────────────── */
void rtu_close(rtu_ctx_t *ctx) {
    if (ctx) {
        close(ctx->fd);
        free(ctx);
    }
}

/* ── Build RTU frame (slave + PDU + CRC) ─────────────── */
int rtu_build_frame(uint8_t slave_id,
                    const uint8_t *pdu, int pdu_len,
                    uint8_t *frame) {
    frame[0] = slave_id;
    memcpy(frame + 1, pdu, (size_t)pdu_len);
    uint16_t crc = rtu_crc16(frame, (size_t)(1 + pdu_len));
    frame[1 + pdu_len]     = (uint8_t)(crc & 0xFF);       /* CRC low */
    frame[1 + pdu_len + 1] = (uint8_t)((crc >> 8) & 0xFF); /* CRC high */
    return 1 + pdu_len + 2;
}

/* ── Validate received RTU frame ─────────────────────── */
int rtu_validate_frame(const uint8_t *frame, int len) {
    if (len < 4) return -1; /* Minimum: slave + FC + 1 data + 2 CRC */
    uint16_t received = (uint16_t)(frame[len-2] | (frame[len-1] << 8));
    uint16_t computed  = rtu_crc16(frame, (size_t)(len - 2));
    if (received != computed) {
        fprintf(stderr, "[rtu] CRC error: got 0x%04X, expected 0x%04X\n",
                received, computed);
        return -1;
    }
    return 0;
}

/* ── RTU transaction with timeout ────────────────────── */
int rtu_transaction(rtu_ctx_t *ctx,
                    const uint8_t *request, int req_len,
                    uint8_t *response, int resp_size,
                    int timeout_ms) {
    /* Flush any stale data */
    tcflush(ctx->fd, TCIFLUSH);

    /* Send request */
    ssize_t written = write(ctx->fd, request, (size_t)req_len);
    if (written != req_len) {
        perror("[rtu] write");
        return -1;
    }
    tcdrain(ctx->fd); /* Wait until all bytes are transmitted */

    /* Receive response with timeout */
    int total = 0;
    struct timeval deadline;
    gettimeofday(&deadline, NULL);
    deadline.tv_usec += timeout_ms * 1000;
    deadline.tv_sec  += deadline.tv_usec / 1000000;
    deadline.tv_usec %= 1000000;

    while (total < resp_size) {
        struct timeval now, remaining;
        gettimeofday(&now, NULL);

        /* Check if deadline passed */
        if (timercmp(&now, &deadline, >)) break;

        timersub(&deadline, &now, &remaining);

        fd_set rds;
        FD_ZERO(&rds);
        FD_SET(ctx->fd, &rds);

        int sel = select(ctx->fd + 1, &rds, NULL, NULL, &remaining);
        if (sel <= 0) break; /* timeout or error */

        ssize_t n = read(ctx->fd, response + total, (size_t)(resp_size - total));
        if (n <= 0) break;
        total += (int)n;

        /* Check if we have a complete frame */
        if (total >= 4 && rtu_validate_frame(response, total) == 0)
            return total;
    }

    if (total > 0)
        fprintf(stderr, "[rtu] Incomplete/invalid frame (%d bytes)\n", total);
    return -1;
}
