#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

/**
 * modbus_tcp.h - Modbus TCP server, MBAP header, PDU conversion
 */

#include <stdint.h>
#include "config.h"

#define MODBUS_TCP_MAX_FRAME  260
#define MODBUS_TCP_HEADER_LEN 6

/* MBAP header (6 bytes) */
typedef struct {
    uint16_t transaction_id;
    uint16_t protocol_id;   /* Always 0 for Modbus */
    uint16_t length;        /* Number of following bytes */
    uint8_t  unit_id;       /* Slave address */
} mbap_header_t;

/* Modbus exception codes */
#define MODBUS_EX_ILLEGAL_FUNCTION       0x01
#define MODBUS_EX_ILLEGAL_DATA_ADDRESS   0x02
#define MODBUS_EX_ILLEGAL_DATA_VALUE     0x03
#define MODBUS_EX_SERVER_DEVICE_FAILURE  0x04
#define MODBUS_EX_GATEWAY_PATH_FAILED    0x0A
#define MODBUS_EX_GATEWAY_TARGET_FAILED  0x0B

/* Opaque server/client types */
typedef struct tcp_server tcp_server_t;
typedef struct {
    int  fd;
    char peer_addr[48];
} tcp_client_t;

/* Server lifecycle */
tcp_server_t *tcp_server_create(int port, int max_clients);
void          tcp_server_destroy(tcp_server_t *srv);

/* Accept with timeout (ms). Returns 0 on new client, -1 on timeout/error */
int tcp_accept(tcp_server_t *srv, tcp_client_t *client, int timeout_ms);
void tcp_close_client(tcp_client_t *client);

/* Receive a Modbus TCP request. Returns PDU length, or <=0 on disconnect */
int tcp_recv_request(tcp_client_t *client,
                     uint8_t *buf, int buf_size,
                     mbap_header_t *mbap_out);

/* Send a Modbus TCP response */
int tcp_send_response(tcp_client_t *client,
                      const uint8_t *buf, int len);

/* Send a Modbus exception response */
int tcp_send_exception(tcp_client_t *client,
                       const mbap_header_t *mbap,
                       uint8_t function_code,
                       uint8_t exception_code);

/* Frame conversion */
int tcp_pdu_to_rtu(const uint8_t *pdu, int pdu_len,
                   uint8_t slave_id, uint8_t *rtu_frame);

int rtu_resp_to_tcp(const uint8_t *rtu_frame, int rtu_len,
                    const mbap_header_t *mbap,
                    uint8_t *tcp_buf, int tcp_buf_size, int *tcp_len);

#endif /* MODBUS_TCP_H */
