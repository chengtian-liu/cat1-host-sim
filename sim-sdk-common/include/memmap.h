/*
 * memmap.h -- host shim
 *
 * Behaves the same as the SDK's platform/include/memmap.h: pulls in the chip
 * memory map (specified by the build macro CONFIG_BOARD_MEMMAP, which the
 * simulator points at memlayout/XY4101PC/memmap.h) and adds the common
 * utility macros.
 *
 * The only reason this shim exists: in the -I order of simulator compilation
 * units it must be hit before platform/include (the xyos shim directory comes
 * first), otherwise the shim versions of the other override headers would not
 * be picked up.
 */

#ifndef __MEMMAP_SHIM_H__
#define __MEMMAP_SHIM_H__

/* defined by build system: memlayout/XY4101PC/memmap.h
 * xyat (the real AT framework) defines this macro; the xyos base kernel only
 * requires this header to exist and falls back to an empty implementation
 * when undefined (Phase A behavior). */
#ifdef CONFIG_BOARD_MEMMAP
#include CONFIG_BOARD_MEMMAP

#include <stdint.h>
#include <stddef.h>

/* Compile-time static assertion, for checking array/struct size overflow */
#define _static_glue2(x, y) x ## y
#define _static_glue(x, y) _static_glue2(x, y)
#define xy_static_assert(exp) \
    typedef char _static_glue(static_assert, __LINE__) [(exp) ? 1 : -1]

#define IS_CP_MEM(a) ((((uint32_t)(size_t)(a)) >= CP_PSRAM_HEAP_START && ((uint32_t)(size_t)(a)) < CP_PSRAM_HEAP_LIMIT))

#define CACHE_LINE_BYTES    32

/* Host memory has no cache-coherency issues, but keep the same check form as
 * the real device (which requires cross-core memory to be allocated with
 * xy_malloc_align and aligned to 32 bytes) */
#define IS_CACHE_ALIGNED(addr)  (((uint32_t)(uintptr_t)(addr) % CACHE_LINE_BYTES) == 0)

/* flash geometry variables: defined in memlayout/XY4101PC/memmap.c (already
 * compiled into xyos). Used from Phase 2 FS onward (fs_al.c/fs_ext_cfg.c/
 * at_fs.c via the WORKING_FS_BASE/USER_FS_FLASH_* macros), matching the
 * extern list of the real-device platform/include/memmap.h item by item */
extern const unsigned int fota_code_prime_len;
extern const unsigned int ap_flash_base_addr;
extern const unsigned int ap_flash_base_len;
extern const unsigned int app_bin_partition_len;
extern const unsigned int working_fs_len;
extern const unsigned int user_fs_flash_base;
extern const unsigned int user_fs_flash_len;
extern const unsigned int user_flash_base;
extern const unsigned int user_flash_len;
extern const unsigned int g_default_fs_base;
extern const unsigned int g_default_fs_len;

#define WORKING_FS_BASE g_default_fs_base
#define WORKING_FS_LEN  g_default_fs_len

#endif /* CONFIG_BOARD_MEMMAP */

#endif /* __MEMMAP_SHIM_H__ */
