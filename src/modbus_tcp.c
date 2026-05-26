/**
 * modbus_tcp.c - Modbus TCP server implementation
 *
 * Handles MBAP header parsing, TCP server accept loop,
 * and PDU conversion between TCP and RTU formats.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "modbus_tcp.h"
#include "modbus_rtu.h"

int g_log_level = LOG_LEVEL_DEFAULT;

/* ── Server type ─────────────────────────────────────── */
struct tcp_server {
    int  listen_fd;
    int  port;
    int  max_clients;
};

/* ── Create TCP server ───────────────────────────────── */
tcp_server_t *tcp_server_create(int port, int max_clients) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("[tcp] socket"); return NULL; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[tcp] bind"); close(fd); return NULL;
    }
    if (listen(fd, max_clients) < 0) {
        perror("[tcp] listen"); close(fd); return NULL;
    }

    tcp_server_t *srv = malloc(sizeof(tcp_server_t));
    if (!srv) { close(fd); return NULL; }
    srv->listen_fd   = fd;
    srv->port        = port;
    srv->max_clients = max_clients;
    return srv;
}

/* ── Destroy server ──────────────────────────────────── */
void tcp_server_destroy(tcp_server_t *srv) {
    if (srv) { close(srv->listen_fd); free(srv); }
}

/* ── Accept with timeout ─────────────────────────────── */
int tcp_accept(tcp_server_t *srv, tcp_client_t *client, int timeout_ms) {
    fd_set rds;
    FD_ZERO(&rds);
    FD_SET(srv->listen_fd, &rds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };

    int sel = select(srv->listen_fd + 1, &rds, NULL, NULL, &tv);
    if (sel <= 0) return -1;

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int cfd = accept(srv->listen_fd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) return -1;

    client->fd = cfd;
    inet_ntop(AF_INET, &peer.sin_addr, client->peer_addr, sizeof(client->peer_addr));
    return 0;
}

/* ── Close client ────────────────────────────────────── */
void tcp_close_client(tcp_client_t *client) {
    if (client && client->fd >= 0) { close(client->fd); client->fd = -1; }
}

/* ── Receive Modbus TCP request ──────────────────────── */
int tcp_recv_request(tcp_client_t *client, uint8_t *buf, int buf_size,
                     mbap_header_t *mbap_out) {
    /* Read MBAP header (7 bytes: 6 + unit_id is part of PDU, but we read all 7) */
    uint8_t hdr[7];
    int n = recv(client->fd, hdr, 7, MSG_WAITALL);
    if (n <= 0) return n;
    if (n < 7)  return -1;

    mbap_out->transaction_id = (uint16_t)((hdr[0] << 8) | hdr[1]);
    mbap_out->protocol_id    = (uint16_t)((hdr[2] << 8) | hdr[3]);
    mbap_out->length         = (uint16_t)((hdr[4] << 8) | hdr[5]);
    mbap_out->unit_id        = hdr[6];

    /* PDU length = length field - 1 (unit_id already read) */
    int pdu_len = mbap_out->length - 1;
    if (pdu_len <= 0 || pdu_len > buf_size) return -1;

    n = recv(client->fd, buf, pdu_len, MSG_WAITALL);
    return (n == pdu_len) ? pdu_len : -1;
}

/* ── Send Modbus TCP response ────────────────────────── */
int tcp_send_response(tcp_client_t *client, const uint8_t *buf, int len) {
    return (int)send(client->fd, buf, (size_t)len, 0);
}

/* ── Send exception response ─────────────────────────── */
int tcp_send_exception(tcp_client_t *client, const mbap_header_t *mbap,
                       uint8_t function_code, uint8_t exception_code) {
    uint8_t frame[9];
    frame[0] = (uint8_t)(mbap->transaction_id >> 8);
    frame[1] = (uint8_t)(mbap->transaction_id & 0xFF);
    frame[2] = 0x00; /* protocol */
    frame[3] = 0x00;
    frame[4] = 0x00; /* length = 3 */
    frame[5] = 0x03;
    frame[6] = mbap->unit_id;
    frame[7] = function_code | 0x80;
    frame[8] = exception_code;
    return (int)send(client->fd, frame, 9, 0);
}

/* ── Convert TCP PDU to RTU frame ────────────────────── */
int tcp_pdu_to_rtu(const uint8_t *pdu, int pdu_len,
                   uint8_t slave_id, uint8_t *rtu_frame) {
    return rtu_build_frame(slave_id, pdu, pdu_len, rtu_frame);
}

/* ── Convert RTU response to TCP frame ───────────────── */
int rtu_resp_to_tcp(const uint8_t *rtu_frame, int rtu_len,
                    const mbap_header_t *mbap,
                    uint8_t *tcp_buf, int tcp_buf_size, int *tcp_len) {
    /* RTU: [slave_id][PDU...][CRC_L][CRC_H]
       TCP: [MBAP 6 bytes][unit_id][PDU...]
       Strip slave_id (first byte) and CRC (last 2 bytes) */
    int pdu_len = rtu_len - 3; /* minus slave_id and 2 CRC bytes */
    if (pdu_len <= 0 || (7 + pdu_len) > tcp_buf_size) return -1;

    uint16_t length = (uint16_t)(1 + pdu_len); /* unit_id + PDU */

    tcp_buf[0] = (uint8_t)(mbap->transaction_id >> 8);
    tcp_buf[1] = (uint8_t)(mbap->transaction_id & 0xFF);
    tcp_buf[2] = 0x00;
    tcp_buf[3] = 0x00;
    tcp_buf[4] = (uint8_t)(length >> 8);
    tcp_buf[5] = (uint8_t)(length & 0xFF);
    tcp_buf[6] = mbap->unit_id;
    memcpy(tcp_buf + 7, rtu_frame + 1, (size_t)pdu_len); /* skip slave_id */

    *tcp_len = 7 + pdu_len;
    return 0;
}
