/*
 * sim_vps_net.h -- public interface of the fake CP control plane
 * (network-class AT commands + PDP activation)
 *
 * Called by the virtual PS dispatcher (xy_atc_data_req in sim_at_shims.c):
 * if the command is a network-class one, it responds and returns 1 (handled);
 * otherwise returns 0 to fall through to the original flow.
 */

#ifndef __SIM_VPS_NET_H__
#define __SIM_VPS_NET_H__

#include <stdint.h>

/**
 * @param cmd    full command, uppercased with trailing \r\n stripped
 *               ('?'/'=' and parameters kept)
 * @param ttyFd  AT channel fd (responses go back through the nearps loop
 *               via SendAtInd2User)
 * @return       1 = handled (response already sent); 0 = not a network
 *               command, untouched
 */
int sim_vps_net_handle(const char *cmd, int ttyFd);

/**
 * Auto-activate PDP at boot (equivalent to sending AT+CGACT=1 automatically,
 * sparing manual typing). Called by sim_boot_task after net_init completes
 * and simbridge is up. Must run in RTOS task context: the internal netifapi
 * calls are only allowed inside tasks.
 *
 * @return 0 = activated (or already activated earlier)
 */
int sim_vps_net_auto_activate(void);

/**
 * Activate the PDP for a given CID (wan_eth_activate, static address
 * assignment, idempotent). AP-side activation requests are routed here:
 *   - the xy_activate_cid / xy_activate_cid_QIACTEX stubs in sim_net_shims.c
 *     (real device goes through LtePs AT+CIDACT/QIACTEX; the host goes
 *     straight to the fake CP data plane)
 * Rejected when not attached (CGATT=0) or cid is outside CID_MIN..CID_MAX.
 *
 * @param cid  CID_MIN_VAL..CID_MAX_VAL
 * @return     0 = activated (activation triggered); -1 = rejected/failed
 */
int sim_vps_activate_cid(uint8_t cid);

#endif /* __SIM_VPS_NET_H__ */
