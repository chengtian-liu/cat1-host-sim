#ifndef SIM_LOG_H
#define SIM_LOG_H

/* Simulator logs: everything goes straight to the host console ("logs printed
 * directly to the front end"). Plan B: all logs go to stderr, stdout carries
 * only the AT data stream; the two can be redirected separately.
 * Asynchronous output since 2026-09-04: callers only push log lines into a
 * bounded ring (drops when full, never blocks); a host writer thread writes to
 * the console window / mirror file uniformly -- a stuck console (e.g. in
 * selection mode) will not drag down the simulation workload. See the file
 * header of sim_log.c for details. */

typedef enum {
    LOG_LVL_DBG = 0,
    LOG_LVL_INF,
    LOG_LVL_WRN,
    LOG_LVL_ERR
} log_lvl_t;

void sim_log_init(void);
void sim_log_shutdown(void);
void sim_log_set_level(int lvl);
void sim_log_print(int lvl, const char *tag, const char *fmt, ...);

/**
 * Log mirror (--logfile): logs still go to the window as usual, and a copy is
 * written to the file at the same time. Each open overwrites the old file
 * (same semantics as 2>); the file has no coloring.
 * @return 0 success, -1 failure
 */
int sim_log_open_mirror(const char *path);
void sim_log_close_mirror(void);

#define LOGD(tag, fmt, ...) sim_log_print(LOG_LVL_DBG, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) sim_log_print(LOG_LVL_INF, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) sim_log_print(LOG_LVL_WRN, tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) sim_log_print(LOG_LVL_ERR, tag, fmt, ##__VA_ARGS__)

#endif /* SIM_LOG_H */
