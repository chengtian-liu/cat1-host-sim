/*
 * sys_assert.h -- host shim (replaces platform/arch/lib/common/inc/sys_assert.h)
 *
 * The real-device default is a while(!(x)) infinite loop; on the host it
 * prints the location and aborts instead, making problems easier to locate.
 */

#ifndef __SYS_ASSERT_H__
#define __SYS_ASSERT_H__

#ifdef __cplusplus
extern "C" {
#endif

void sim_assert_fail(const char *file, int line, const char *expr);

#define Sys_Assert(x)   do { if (!(x)) sim_assert_fail(__FILE__, __LINE__, #x); } while (0)

#define sys_assert_set_tp_base()
#define sys_assert_get_scene(buffer, size)      (0)
#define sys_assert_get_sp(hartid)               (0)

#ifdef __cplusplus
}
#endif

#endif /* __SYS_ASSERT_H__ */
