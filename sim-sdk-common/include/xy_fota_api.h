/*
 * xy_fota_api.h -- host override version (for simulator builds only;
 * the SDK original is unaffected)
 *
 * The real-device version lives at platform/application/fota/xy_fota/inc/
 * xy_fota_api.h and pulls in the whole FOTA verification/diff algorithm set,
 * which the simulator does not need. main_proxy.c does include this file,
 * but the FOTA code paths are gated by SOFT_RESET+SOFT_RB_BY_FOTA and are
 * only entered after a real-device OTA reboot, which never happens on the
 * host -- so an empty header is enough to let the build pass.
 */

#ifndef XY_FOTA_API_H
#define XY_FOTA_API_H



#endif /* XY_FOTA_API_H */
