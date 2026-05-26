/**
 * gateway.c - Modbus RTU/TCP Gateway
 *
 * Bridges Modbus RTU (serial RS-485) with Modbus TCP (Ethernet).
 * Designed for embedded Linux targets (Raspberry Pi, BeagleBone, etc.)
 *
 * Architecture:
 *   - One thread listens for TCP connections (Modbus TCP server)
 *   - Each TCP request is serialized and forwarded to RTU slave
 *   - RTU response is converted back to TCP and returned to client
 *
 * Build: see Makefile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>

#include "modbus_rtu.h"
#include "modbus_tcp.h"
#include "config.h"

/* ── Global state ─────────────────────────────────────── */
static volatile int g_running = 1;
static pthread_mutex_t g_rtu_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    rtu_ctx_t   *rtu;
    tcp_client_t client;
} worker_args_t;

/* ── Signal handler ──────────────────────────────────── */
static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
    fprintf(stderr, "\n[gateway] Shutting down...\n");
}

/* ── Per-client worker thread ────────────────────────── */
static void *client_worker(void *arg) {
    worker_args_t *wa = (worker_args_t *)arg;
    uint8_t tcp_buf[MODBUS_TCP_MAX_FRAME];
    uint8_t rtu_req[MODBUS_RTU_MAX_FRAME];
    uint8_t rtu_resp[MODBUS_RTU_MAX_FRAME];
    int     tcp_len, rtu_len, resp_len;
    mbap_header_t mbap;

    while (g_running) {
        /* 1. Receive Modbus TCP request */
        tcp_len = tcp_recv_request(&wa->client, tcp_buf, sizeof(tcp_buf), &mbap);
        if (tcp_len <= 0) break; /* client disconnected */

        /* 2. Convert TCP PDU -> RTU frame (add slave ID + CRC) */
        rtu_len = tcp_pdu_to_rtu(tcp_buf, tcp_len, mbap.unit_id, rtu_req);
        if (rtu_len < 0) {
            LOG_WARN("Failed to convert TCP->RTU, skipping");
            continue;
        }

        /* 3. Forward to RTU slave (serialized - only one RTU bus) */
        pthread_mutex_lock(&g_rtu_mutex);
        resp_len = rtu_transaction(wa->rtu, rtu_req, rtu_len,
                                   rtu_resp, sizeof(rtu_resp),
                                   RTU_TIMEOUT_MS);
        pthread_mutex_unlock(&g_rtu_mutex);

        if (resp_len < 0) {
            /* RTU timeout or error - send Modbus exception back */
            LOG_WARN("RTU timeout for slave %d FC=0x%02X",
                     mbap.unit_id, rtu_req[1]);
            tcp_send_exception(&wa->client, &mbap, rtu_req[1],
                               MODBUS_EX_GATEWAY_TARGET_FAILED);
            continue;
        }

        /* 4. Convert RTU response -> TCP frame and send */
        if (rtu_resp_to_tcp(rtu_resp, resp_len, &mbap,
                            tcp_buf, sizeof(tcp_buf), &tcp_len) == 0) {
            tcp_send_response(&wa->client, tcp_buf, tcp_len);
        }
    }

    tcp_close_client(&wa->client);
    free(wa);
    return NULL;
}

/* ── Usage ───────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --rtu      <device>   Serial device (default: /dev/ttyUSB0)\n"
        "  --baud     <rate>     Baud rate (default: 9600)\n"
        "  --parity   <N|E|O>    Parity (default: N)\n"
        "  --stop     <1|2>      Stop bits (default: 1)\n"
        "  --tcp-port <port>     TCP listen port (default: 502)\n"
        "  --verbose             Enable verbose logging\n"
        "  --help                Show this help\n",
        prog);
}

/* ── Main ────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    /* Defaults */
    char rtu_device[64] = DEFAULT_RTU_DEVICE;
    int  baud_rate       = DEFAULT_BAUD_RATE;
    char parity          = 'N';
    int  stop_bits       = 1;
    int  tcp_port        = DEFAULT_TCP_PORT;
    int  verbose         = 0;

    /* Parse arguments */
    static struct option long_opts[] = {
        {"rtu",      required_argument, 0, 'r'},
        {"baud",     required_argument, 0, 'b'},
        {"parity",   required_argument, 0, 'p'},
        {"stop",     required_argument, 0, 's'},
        {"tcp-port", required_argument, 0, 't'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "r:b:p:s:t:vh", long_opts, &idx)) != -1) {
        switch (opt) {
            case 'r': strncpy(rtu_device, optarg, sizeof(rtu_device)-1); break;
            case 'b': baud_rate = atoi(optarg); break;
            case 'p': parity = optarg[0]; break;
            case 's': stop_bits = atoi(optarg); break;
            case 't': tcp_port = atoi(optarg); break;
            case 'v': verbose = 1; break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }

    if (verbose) log_set_level(LOG_DEBUG);

    printf("[gateway] Starting Modbus RTU/TCP Gateway\n");
    printf("[gateway]   RTU device : %s @ %d baud (%c,%d)\n",
           rtu_device, baud_rate, parity, stop_bits);
    printf("[gateway]   TCP port   : %d\n", tcp_port);

    /* Install signal handlers */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* Open RTU serial port */
    rtu_ctx_t *rtu = rtu_open(rtu_device, baud_rate, parity, stop_bits);
    if (!rtu) {
        fprintf(stderr, "[gateway] ERROR: Cannot open RTU device '%s': %s\n",
                rtu_device, strerror(errno));
        return 1;
    }
    printf("[gateway] RTU port opened.\n");

    /* Open TCP server */
    tcp_server_t *srv = tcp_server_create(tcp_port, MAX_TCP_CLIENTS);
    if (!srv) {
        fprintf(stderr, "[gateway] ERROR: Cannot bind TCP port %d: %s\n",
                tcp_port, strerror(errno));
        rtu_close(rtu);
        return 1;
    }
    printf("[gateway] Listening on TCP port %d. Press Ctrl+C to stop.\n", tcp_port);

    /* Main accept loop */
    while (g_running) {
        tcp_client_t client;
        if (tcp_accept(srv, &client, 1000 /* ms timeout */) == 0) {
            LOG_INFO("New TCP client connected from %s", client.peer_addr);

            worker_args_t *wa = malloc(sizeof(worker_args_t));
            if (!wa) { tcp_close_client(&client); continue; }
            wa->rtu    = rtu;
            wa->client = client;

            pthread_t tid;
            if (pthread_create(&tid, NULL, client_worker, wa) != 0) {
                LOG_WARN("Failed to create worker thread");
                free(wa);
                tcp_close_client(&client);
            } else {
                pthread_detach(tid);
            }
        }
    }

    /* Cleanup */
    tcp_server_destroy(srv);
    rtu_close(rtu);
    pthread_mutex_destroy(&g_rtu_mutex);
    printf("[gateway] Stopped cleanly.\n");
    return 0;
}
