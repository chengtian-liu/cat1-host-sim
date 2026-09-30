/*
 * sim_main.h -- generic startup skeleton and target hooks
 *
 * The host simulator's main() (CLI parsing / crash diagnostics / log init /
 * three-channel binding / kernel start) is identical across all SDK family
 * products and is implemented in sim-sdk-common/src/sim_main.c.
 * Only three chip-specific points remain, injected via the g_sim_target hook
 * table from the target-xxx directory:
 *
 *   1. product / byline / coverage -- product text for the startup banner;
 *   2. boot_task -- RTOS startup task body (SDK subsystem init order, bridge binding);
 *   3. factory_nv_init -- chip-specific pin overrides of factory NV defaults (optional).
 *
 * Adding a product = create a target-xxx directory and implement one hook
 * table + boot_task.
 */
#ifndef SIM_MAIN_H
#define SIM_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "factory_nv.h"

/*
 * Target hook table. target-xxx must define a const instance named g_sim_target
 * (resolved at link time; if missing, the link fails, reminding the porter to
 * fill it in).
 */
typedef struct {
    /* ---- banner text ---- */
    const char *product;      /* product name, e.g. "XY4101 LTE Cat.1 Host Simulator" */
    const char *byline;       /* author line, e.g. "by Chengtian Liu" */
    const char *coverage[2];  /* list of what the real code covers (two lines; NULL omits a line) */

    /* ---- startup task body ----
     * Created by the skeleton via osThreadNew after osKernelInitialize
     * (name "simboot", priority osPriorityNormal, stack 8192). The task
     * performs SDK subsystem initialization, sim-core callback binding,
     * auto-dial, etc., and ends with osThreadExit().
     * Skeleton guarantee: when this task starts, the log ring, the
     * three-channel backends, and the virtual tty devices are all ready. */
    void (*boot_task)(void *arg);

    /* ---- factory NV chip overrides (optional; pass NULL for no override) ----
     * sim_fac_nv in sim_factory_nv.c holds the family-common defaults (all
     * pin-class fields are 255 = not connected). Chip-specific pin assignments
     * (JTAG / debug UART / SIM / power control, etc.) are injected via this
     * callback. Called in sim_main() after the banner and before boot_task is
     * created, ensuring at_init() reads a complete NV. */
    void (*factory_nv_init)(softap_fac_nv_t *nv);
} sim_target_hooks_t;

extern const sim_target_hooks_t g_sim_target;

/*
 * Generic startup skeleton entry (CLI parsing -> log/channels -> osKernelStart,
 * never returns). The executable entry file in target-xxx defines main to call
 * this function directly:
 *     int main(int argc, char **argv) { return sim_main(argc, argv); }
 * (main does not go into the static library -- linker behavior when pulling
 * main from an archive is unreliable.)
 * g_sim_target must be defined before calling.
 */
int sim_main(int argc, char **argv);

/*
 * Absolute path resolved from --pcap (for boot_task to open the capture file).
 * @return the path; empty string "" when --pcap was not given.
 */
const char *sim_main_pcap_path(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_MAIN_H */
