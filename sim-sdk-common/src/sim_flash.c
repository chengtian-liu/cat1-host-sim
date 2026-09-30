/*
 * sim_flash.c - host implementation of the xy_flash_* off-chip flash
 * interfaces (foundation of the FS layer)
 *
 * Real device: boot/drivers/flash_adapt (NOR flash driver). The host has
 * no flash; here we emulate the **FS partition** segment:
 *
 *   [WORKING_FS_BASE, WORKING_FS_BASE + WORKING_FS_LEN)
 *   memmap.c: 720KB starting at 0x60337000 (working_fs_len=0xB4000),
 *   a region not erased by FOTA
 *
 * Form = RAM buffer + write-through to a disk image file:
 *   <exe directory>\xysim_fs.img
 * Mapping to the real device flash: survives power loss (image file),
 * erased state is all 0xFF, NOR writes can only clear bits, never set
 * them (xy_flash_write emulates AND semantics).
 *
 * Callers: the littlefs block-device layer (fs_al.c / fs_ext_cfg.c) is
 * the only user at this stage; the whole-region erase used by QFFORMAT
 * (end of fs_al.c) also falls inside the region. Out-of-region accesses
 * (other partitions on the real device: FOTA/user flash) are logged as
 * errors and ignored - the host does not emulate those partitions.
 *
 * The image file is loaded lazily on first access: if it does not exist
 * it is created and filled with 0xFF (= blank flash; littlefs first
 * mount fails -> auto format -> remount, matching real-device first-boot
 * behavior).
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"
#include "memmap.h"       /* WORKING_FS_BASE / WORKING_FS_LEN */

#include "sim_log.h"

#define SIM_FLASH_SECTOR   4096u

static unsigned char *s_img;          /* RAM buffer (entire FS partition) */
static FILE          *s_fp;           /* image file (write-through), may be NULL = RAM-only */
static uint32_t       s_len;          /* = WORKING_FS_LEN */
static uint32_t       s_base;         /* = WORKING_FS_BASE */
static osMutexId_t    s_mux;
static char           s_path[MAX_PATH];

static void sim_flash_lock(void)
{
    if (s_mux != NULL && osKernelGetState() == osKernelRunning)
        osMutexAcquire(s_mux, osWaitForever);
}

static void sim_flash_unlock(void)
{
    if (s_mux != NULL && osKernelGetState() == osKernelRunning)
        osMutexRelease(s_mux);
}

/* Image file path: same directory as the exe (independent of the launch
 * directory / elevated-copy working directory) */
static void sim_flash_img_path(void)
{
    char exe[MAX_PATH];
    char *slash;

    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0)
        exe[0] = '\0';
    slash = strrchr(exe, '\\');
    if (slash != NULL)
        slash[1] = '\0';
    snprintf(s_path, sizeof(s_path), "%sxysim_fs.img", exe);
}

/* Lazy initialization: load/create the image on first flash access */
static int sim_flash_ensure(void)
{
    if (s_img != NULL)
        return 0;

    if (osKernelGetState() == osKernelRunning && s_mux == NULL) {
        osMutexAttr_t attr = {0};

        attr.name = "simflash";
        s_mux = osMutexNew(&attr);
    }

    s_base = (uint32_t)(uintptr_t)WORKING_FS_BASE;
    s_len  = WORKING_FS_LEN;

    s_img = (unsigned char *)malloc(s_len);
    if (s_img == NULL) {
        LOGE("SIMFLASH", "cannot allocate %u bytes for FS partition", s_len);
        return -1;
    }

    sim_flash_img_path();
    s_fp = fopen(s_path, "rb");
    if (s_fp != NULL) {
        if (fread(s_img, 1, s_len, s_fp) != s_len) {
            /* Image incomplete: start over as blank flash (littlefs will auto format) */
            LOGW("SIMFLASH", "image '%s' truncated, resetting to erased state", s_path);
            memset(s_img, 0xFF, s_len);
        }
        fclose(s_fp);
    } else {
        memset(s_img, 0xFF, s_len);   /* blank flash: all 0xFF */
    }

    /* Keep it open with r+b: both read and write are needed; create if missing */
    s_fp = fopen(s_path, "r+b");
    if (s_fp == NULL) {
        s_fp = fopen(s_path, "w+b");
    }
    if (s_fp == NULL) {
        LOGW("SIMFLASH", "cannot open image '%s' (err=%lu) - RAM-only, content lost on exit",
             s_path, GetLastError());
    } else {
        /* Write the initial content to disk on first creation / after truncation */
        fwrite(s_img, 1, s_len, s_fp);
        fflush(s_fp);
    }

    LOGI("SIMFLASH", "FS partition up: 0x%08x +%u bytes, image=%s",
         s_base, s_len, s_path);
    return 0;
}

/* Physical address -> image offset; returns -1 if out of bounds
 * (partitions the host does not emulate) */
static int sim_flash_off(const void *flash_addr, uint32_t size, uint32_t *off)
{
    uint32_t a = (uint32_t)(uintptr_t)flash_addr;

    if (a < s_base || a + size > s_base + s_len) {
        LOGE("SIMFLASH", "access outside FS partition: addr=0x%08x len=%u (FS=0x%08x+%u)",
             a, size, s_base, s_len);
        return -1;
    }
    *off = a - s_base;
    return 0;
}

/* Write through to the image after a change: the filesystem survives a
 * crash / hard kill */
static void sim_flash_persist(uint32_t off, uint32_t size)
{
    if (s_fp == NULL)
        return;
    fseek(s_fp, (long)off, SEEK_SET);
    fwrite(s_img + off, 1, size, s_fp);
    fflush(s_fp);
}

/* ------------------------------------------------------------------ */
/* The four xy_flash.h APIs (real device: boot/drivers/flash_adapt)     */
/* ------------------------------------------------------------------ */

void xy_flash_read(void *flash_addr, void *ram_addr, uint32_t size)
{
    uint32_t off;

    sim_flash_lock();
    if (sim_flash_ensure() == 0 && sim_flash_off(flash_addr, size, &off) == 0)
        memcpy(ram_addr, s_img + off, size);
    sim_flash_unlock();
}

void xy_flash_write_no_erase(void *flash_addr, void *ram_addr, uint32_t size)
{
    uint32_t off;

    sim_flash_lock();
    if (sim_flash_ensure() == 0 && sim_flash_off(flash_addr, size, &off) == 0) {
        memcpy(s_img + off, ram_addr, size);
        sim_flash_persist(off, size);
    }
    sim_flash_unlock();
}

void xy_flash_erase(void *flash_addr, uint32_t size)
{
    uint32_t off;

    /* Real device: 4K aligned, rounds up to a multiple of 4K */
    size = (size + SIM_FLASH_SECTOR - 1) & ~(SIM_FLASH_SECTOR - 1);

    sim_flash_lock();
    if (sim_flash_ensure() == 0 && sim_flash_off(flash_addr, size, &off) == 0) {
        memset(s_img + off, 0xFF, size);
        sim_flash_persist(off, size);
    }
    sim_flash_unlock();
}

void xy_flash_write(void *flash_addr, void *ram_addr, uint32_t size)
{
    uint32_t off;
    uint32_t i;
    const unsigned char *src = (const unsigned char *)ram_addr;

    sim_flash_lock();
    if (sim_flash_ensure() == 0 && sim_flash_off(flash_addr, size, &off) == 0) {
        /* NOR semantics: writes can only clear bits, never set them
         * (unerased regions keep their original values) */
        for (i = 0; i < size; i++)
            s_img[off + i] &= src[i];
        sim_flash_persist(off, size);
    }
    sim_flash_unlock();
}

/* ------------------------------------------------------------------ */
/* Flash protection switches (same file on the real device; the host has */
/* no protection concept, so these are no-ops)                         */
/* ------------------------------------------------------------------ */

void flash_protect_enable(void)
{
}

void flash_protect_disable(void)
{
}
