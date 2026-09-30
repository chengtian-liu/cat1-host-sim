/*
 * ux_device_class_api.h -- host override (empty stub)
 *
 * The real-device version lives in platform/kernel/usbx/ux_app/inc/ and pulls
 * in the whole USBX device class API; the host does not carry USBX.
 * ppp_server.c uses ux_device_cdc_acm_modem_recv_free, so the declaration is
 * provided here; the implementation in sim_at_shims.c is a no-op.
 */
#ifndef SIM_HOST_UX_DEVICE_CLASS_API_H
#define SIM_HOST_UX_DEVICE_CLASS_API_H

#ifdef __cplusplus
extern "C" {
#endif

void ux_device_cdc_acm_modem_recv_free(void *buffer);

#ifdef __cplusplus
}
#endif

#endif /* SIM_HOST_UX_DEVICE_CLASS_API_H */