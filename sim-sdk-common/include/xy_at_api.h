/*
 * xy_at_api.h -- host shim (Phase A/B only, replaces
 * platform/application/at_ctrl/inc/xy_at_api.h)
 *
 * tasks.c / timers.c #include "xy_at_api.h", but the only symbol actually
 * used (send_debug_str_to_ext) sits in #if 0 dead code. The real header
 * would pull in the whole AT framework header set (at_com.h/at_error.h/...),
 * which is not provided during the OS-layer compile stage.
 *
 * When Phase C compiles the real AT framework, the -I order for that target
 * puts at_ctrl/inc before sim-sdk-common/include, so the real header
 * naturally shadows this shim.
 */

#ifndef __XY_AT_API_SHIM_H__
#define __XY_AT_API_SHIM_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Real declaration lives at at_ctrl/inc/xy_at_api.h:189; this one only
 * keeps the kernel compiling */
void send_debug_str_to_ext(char *buf);

#ifdef __cplusplus
}
#endif

#endif /* __XY_AT_API_SHIM_H__ */
