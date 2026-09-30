/*
 * sim_pcap.c - host-side IP packet capture implementation (classic pcap,
 * readable directly by Wireshark)
 *
 * Call chain and threading model: see sim_pcap.h. Format essentials:
 *   - global header: magic 0xA1B2C3D4 (native byte order), v2.4,
 *     LINKTYPE_ETHERNET=1
 *   - per packet: 16B record header (host wall-clock timestamp) + 14B
 *     fake Ethernet header + raw IP datagram
 *   - fake Ethernet header: all-zero MACs, EtherType 0x0800 / 0x86DD
 *     chosen by IP version
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"

#include "sim_log.h"
#include "sim_pcap.h"

/* FILETIME (1601, 100ns units) -> Unix epoch second offset */
#define FILETIME_TO_UNIX_SEC 11644473600ULL

#define PCAP_MAGIC          0xA1B2C3D4u
#define PCAP_LINKTYPE_ETH   1u          /* LINKTYPE_ETHERNET */
#define PCAP_SNAPLEN        65535u

#define ETH_HDR_LEN         14
#define ETH_TYPE_IPV4       0x0800u
#define ETH_TYPE_IPV6       0x86DDu

typedef struct {
    uint32_t magic;
    uint16_t v_major;
    uint16_t v_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_file_hdr_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_rec_hdr_t;

static FILE *s_fp;
static osMutexId_t s_mux;
static char s_path[260];

static void pcap_host_time(uint32_t *sec, uint32_t *usec)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    uint64_t unix_usec;

    GetSystemTimeAsFileTime(&ft);
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;

    unix_usec = u.QuadPart / 10ULL;             /* 100ns -> us */
    *sec  = (uint32_t)(unix_usec / 1000000ULL - FILETIME_TO_UNIX_SEC);
    *usec = (uint32_t)(unix_usec % 1000000ULL);
}

static void pcap_lock(void)
{
    /* Mutual exclusion only while the scheduler runs; on atexit/early paths
     * with the kernel not running there can be no concurrent threads */
    if (s_mux != NULL && osKernelGetState() == osKernelRunning)
        osMutexAcquire(s_mux, osWaitForever);
}

static void pcap_unlock(void)
{
    if (s_mux != NULL && osKernelGetState() == osKernelRunning)
        osMutexRelease(s_mux);
}

int sim_pcap_open(const char *path)
{
    pcap_file_hdr_t fh;

    if (path == NULL || path[0] == '\0')
        return -1;

    /* Repeated open: close the old file first; the lock is reused */
    sim_pcap_close();

    if (s_mux == NULL && osKernelGetState() != osKernelInactive) {
        osMutexAttr_t attr = {0};

        attr.name = "simpkap";
        s_mux = osMutexNew(&attr);
    }

    s_fp = fopen(path, "wb");
    if (s_fp == NULL) {
        LOGE("SIMPCAP", "cannot open '%s' for capture", path);
        return -1;
    }

    memset(&fh, 0, sizeof(fh));
    fh.magic    = PCAP_MAGIC;
    fh.v_major  = 2;
    fh.v_minor  = 4;
    fh.snaplen  = PCAP_SNAPLEN;
    fh.network  = PCAP_LINKTYPE_ETH;

    if (fwrite(&fh, sizeof(fh), 1, s_fp) != 1) {
        LOGE("SIMPCAP", "cannot write pcap header to '%s'", path);
        fclose(s_fp);
        s_fp = NULL;
        return -1;
    }
    fflush(s_fp);

    snprintf(s_path, sizeof(s_path), "%s", path);
    LOGI("SIMPCAP", "capture on: %s (linktype=EN10MB, UL+DL merged)", s_path);
    return 0;
}

void sim_pcap_write(const void *data, unsigned len, int dir)
{
    pcap_rec_hdr_t rh;
    unsigned char eth[ETH_HDR_LEN];
    unsigned int ver;

    (void)dir;   /* merged into a single file; direction identified by the local address (sim_proxy subnet 10.0.0.2) */

    if (s_fp == NULL || data == NULL || len == 0)
        return;

    ver = ((const unsigned char *)data)[0] >> 4;

    /* Fake Ethernet header: all-zero MACs + EtherType by IP version */
    memset(eth, 0, sizeof(eth));
    if (ver == 4) {
        eth[12] = (unsigned char)(ETH_TYPE_IPV4 >> 8);
        eth[13] = (unsigned char)(ETH_TYPE_IPV4 & 0xFF);
    } else if (ver == 6) {
        eth[12] = (unsigned char)(ETH_TYPE_IPV6 >> 8);
        eth[13] = (unsigned char)(ETH_TYPE_IPV6 & 0xFF);
    } else {
        return;   /* the hook side already guarantees IP; this is a fallback */
    }

    pcap_lock();
    if (s_fp != NULL) {
        pcap_host_time(&rh.ts_sec, &rh.ts_usec);
        rh.incl_len = ETH_HDR_LEN + len;
        rh.orig_len = ETH_HDR_LEN + len;

        fwrite(&rh, sizeof(rh), 1, s_fp);
        fwrite(eth, sizeof(eth), 1, s_fp);
        fwrite(data, 1, len, s_fp);
        fflush(s_fp);   /* captured data survives a crash/hard kill */
    }
    pcap_unlock();
}

void sim_pcap_close(void)
{
    pcap_lock();
    if (s_fp != NULL) {
        fclose(s_fp);
        s_fp = NULL;
        LOGI("SIMPCAP", "capture off: %s", s_path);
    }
    pcap_unlock();
}