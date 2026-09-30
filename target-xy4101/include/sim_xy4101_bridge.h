/*
 * sim_xy4101_bridge.h -- XY4101 bridge layer declarations
 */

#ifndef SIM_XY4101_BRIDGE_H
#define SIM_XY4101_BRIDGE_H

/*
 * Binds the sim-core callback table to XY4101 SDK functions and creates the
 * proxy_rx FreeRTOS task. Must be called after sim_proxy_start() (callback
 * binding may happen before or after, but the proxy_rx task must be created
 * only after sim_proxy_start() has succeeded).
 */
void xy4101_bridge_init(void);

/*
 * Creates and starts the proxy_rx FreeRTOS task (which calls
 * sim_core_proxy_rx_loop internally). Must be called after sim_proxy_start()
 * has succeeded.
 */
void xy4101_proxy_rx_start(void);

#endif /* SIM_XY4101_BRIDGE_H */
