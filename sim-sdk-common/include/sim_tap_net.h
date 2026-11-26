/*
 * sim_tap_net.h -- TAP virtual NIC transceiver (USB ECM/RNDIS gateway path)
 */

#ifndef SIM_TAP_NET_H
#define SIM_TAP_NET_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open the TAP-Windows Adapter V9 named "SimRNDIS" (create one with
 * tools/tap_adapter.py) and start the uplink (TAP -> usbEthDataInput) and
 * downlink (ux_network_send -> TAP) transceiver threads.
 *
 * @return 0 on success, -1 if no usable TAP device was found or opened.
 */
int sim_tap_net_start(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_TAP_NET_H */
