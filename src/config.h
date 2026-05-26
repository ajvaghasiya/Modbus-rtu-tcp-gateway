#ifndef CONFIG_H
#define CONFIG_H

/**
 * config.h - Compile-time configuration for Modbus RTU/TCP Gateway
 * Edit these values before building to match your hardware/network setup.
 */

/* ── Serial / RTU ────────────────────────────────────── */
#define DEFAULT_RTU_DEVICE   "/dev/ttyUSB0"
#define DEFAULT_BAUD_RATE    9600
#define RTU_TIMEOUT_MS       500     /* Max wait for RTU slave response */

/* ── TCP Server ──────────────────────────────────────── */
#define DEFAULT_TCP_PORT     502     /* Standard Modbus TCP port (requires root, or use 1502) */
#define MAX_TCP_CLIENTS      8       /* Max concurrent TCP connections */

/* ── Logging ─────────────────────────────────────────── */
#define LOG_DEBUG  0
#define LOG_INFO   1
#define LOG_WARN   2
#define LOG_ERROR  3

#ifndef LOG_LEVEL_DEFAULT
#define LOG_LEVEL_DEFAULT LOG_INFO
#endif

/* Simple logging macros */
#include <stdio.h>
extern int g_log_level;
static inline void log_set_level(int level) { extern int g_log_level; g_log_level = level; }

#define LOG_DEBUG(fmt, ...) do { if (g_log_level <= LOG_DEBUG) \
    fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_INFO(fmt, ...)  do { if (g_log_level <= LOG_INFO)  \
    fprintf(stdout, "[INFO]  " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_WARN(fmt, ...)  do { if (g_log_level <= LOG_WARN)  \
    fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_ERROR(fmt, ...) do { if (g_log_level <= LOG_ERROR) \
    fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)

#endif /* CONFIG_H */
