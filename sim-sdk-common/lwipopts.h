/**
 * @file lwipopts.h
 * @brief lwip user configuration file
 * @version 1.0
 * @date 2022-05-12
 * @copyright Copyright (c) 2022  Xinyi Information Technology Co., Ltd.
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 */

#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* ===========================================================================
 * Simulator-modified version (sim-sdk-common/lwipopts.h, shadows the SDK's
 * original lwip_config). Copied from the SDK lwip_config/lwipopts.h; only
 * the items below are changed, everything else is kept as-is:
 *   1. IP_ROUTE 1 -> 0, and LWIP_HOOK_IP4_ROUTE / LWIP_HOOK_IP6_ROUTE
 *   disabled (how to enable LWIP_DEBUG for troubleshooting: see the
 *   Lwip Debug options section at the end of this file)
 *      (the simulator has a single WAN netif and needs no multi-PDP route
 *        table; this removes the dependency on ip4_find_route_if /
 *        ip6_static_route)
 *   2. LWIP_HOOK_TCP_ISN disabled (removes the dependency on xy_lwip_tcp_isn;
 *      lwip falls back to its built-in counter)
 *   3. LWIP_DHCP stays 0 (the PS simulator assigns addresses directly;
 *      no lwIP DHCP client needed)
 *   4. LWIP_IPV6 stays 1 unchanged: the SDK's modified api_msg.c (the DNS
 *      dual-stack sorting code inside the LWIP_XY_CUSTOM block) references
 *      ip_addr_t.type directly and is written assuming dual-stack; disabling
 *      IPV6 would break the build. For the initial phase, PDPs use
 *      psIpType=IPV4 to sidestep IPv6.
 * =========================================================================== */

#include <stdint.h>

/*
   --------------------------------
   ---------- IP options ----------
   --------------------------------
*/
/**
 * IP_REASSEMBLY==1: Reassemble incoming fragmented IP packets. Note that
 * this option does not affect outgoing packet sizes, which can be controlled
 * via IP_FRAG.
 */
#define IP_REASSEMBLY                   1

/**
 * IP_FRAG==1: Fragment outgoing IP packets if their size exceeds MTU. Note
 * that this option does not affect incoming packet sizes, which can be
 * controlled via IP_REASSEMBLY.
 */
#define IP_FRAG                         1

/**
 * IP_FORWARD==1: Enables the ability to forward IP packets across network
 * interfaces. If you are going to run lwIP on a device with only one network
 * interface, define this to 0.
 * Route forwarding feature, usually associated with IP_ROUTE, finds the corresponding network interface based on the destination IP address. Currently not available
 */
#define IP_FORWARD                      1

/**
 * IP_NAT==1: Enables Network address translation for packet routing between
 * local and external interface. If you are going to run lwIP on a device with
 * only one network interface, define this to 0.
 * Subnet mode, NAT translation is required for AP core connections or PC terminal connections. Currently, it is assumed that AP core and PC terminal will not connect to the same remote server, so NAT is not needed; IP_FAST_PROC fast processing is sufficient
 */
#define IP_NAT                          0

/**
 * IP_ROUTE==1: Support IPv4 route.
 * When multiple external network interfaces exist, select the external interface based on the destination IP address subnet, typically used for multi-PDP activation scenarios
 */
#define IP_ROUTE                        0

/**
 * LWIP_HOOK_IP4_ROUTE(dest):
 * Called from ip_route() (IPv4)
 * Signature:\code{.c}
 *   struct netif *my_hook(const ip4_addr_t *dest);
 * \endcode
 * Arguments:
 * - dest: destination IPv4 address
 * Returns values:
 * - the destination netif
 * - NULL if no destination netif is found. In that case, ip_route() continues as normal.
 */
/* Simulator modification: disabled; with a single netif the default route suffices */
/* #define LWIP_HOOK_IP4_ROUTE(dest)      ip4_find_route_if(dest) */

/*
   ---------------------------------
   ---------- ARP options ----------
   ---------------------------------
*/
/**
 * ARP_QUEUEING==1: Multiple outgoing packets are queued during hardware address
 * resolution. By default, only the most recent packet is queued per IP address.
 * This is sufficient for most protocols and mainly reduces TCP connection
 * startup time. Set this to 1 if you know your application sends more than one
 * packet in a row to an IP address that is not in the ARP cache.
 */
#define ARP_QUEUEING                    0

/**
 * LWIP_ARP==1: Enable ARP functionality.
 */
#define LWIP_ARP                        (USB_DEVICE && (USB_ECM || USB_RNDIS))

/** LWIP_ETHERNET==1: enable ethernet support even though ARP might be disabled
 */
#define LWIP_ETHERNET                   1

/**
 * LWIP_PPP==1: Enable PPP support (PPP netif + lwip PPP protocol stack).
 * Needed when compiling ppp_server.c directly.
 */
#define LWIP_PPP                        1

/**
 * ARP_TABLE_SIZE: Number of active MAC-IP address pairs cached.
 */
#define ARP_TABLE_SIZE                  2

/** ETHARP_SUPPORT_STATIC_ENTRIES==1: enable code to support static ARP table
 * entries (using etharp_add_static_entry/etharp_remove_static_entry).
 */
#define ETHARP_SUPPORT_STATIC_ENTRIES   1

#define LWIP_ARP_AGING_INTERVAL         3


/*
   ---------------------------------
   ---------- TCP options ----------
   ---------------------------------
*/
/**
 * TCP_LISTEN_BACKLOG: Enable the backlog option for tcp listen pcb.
 */
#define TCP_LISTEN_BACKLOG              1

/**
 * LWIP_TCP==1: Turn on TCP.
 */
#define LWIP_TCP                        1

/**
 * TCP_TTL: Default Time-To-Live value.
 */
#define TCP_TTL                         255

/**
 * TCP_MSS: TCP Maximum segment size. (default is 536, a conservative default,
 * you might want to increase this.)
 * For the receive side, this MSS is advertised to the remote side
 * when opening a connection. For the transmit size, this MSS sets
 * an upper limit on the MSS advertised by the remote host.
 */
#define TCP_MSS                         1460

/**
 * TCP_SND_BUF: TCP sender buffer space (bytes).
 * To achieve good performance, this should be at least 2 * TCP_MSS.
 */
#define TCP_SND_BUF                     (8*TCP_MSS)

/**
 * TCP_SND_QUEUELEN: TCP sender buffer space (pbufs). This must be at least
 * as much as (2 * TCP_SND_BUF/TCP_MSS) for things to work.
 */
#define TCP_SND_QUEUELEN                (2*TCP_SND_BUF/TCP_MSS)

/**
 * TCP_WND: The size of a TCP window.  This must be at least
 * (2 * TCP_MSS) for things to work well.
 * ATTENTION: when using TCP_RCV_SCALE, TCP_WND is the total size
 * with scaling applied. Maximum window value in the TCP header
 * will be TCP_WND >> TCP_RCV_SCALE
 */
extern uint16_t g_local_tcp_rcv_wnd;
#define TCP_WND                         g_local_tcp_rcv_wnd
#define DEFAULT_TCP_WND                 65535               // TCP proxies in some regional base stations have a 65K window limit

/**
 * LWIP_TCP_SACK_OUT==1: TCP will support sending selective acknowledgements (SACKs).
 */

#define LWIP_TCP_SACK_OUT               1

/**
 * LWIP_WND_SCALE and TCP_RCV_SCALE:
 * Set LWIP_WND_SCALE to 1 to enable window scaling.
 * Set TCP_RCV_SCALE to the desired scaling factor (shift count in the
 * range of [0..14]).
 * When LWIP_WND_SCALE is enabled but TCP_RCV_SCALE is 0, we can use a large
 * send window while having a small receive window only.
 */
#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   1

/**
 * TCP_SYNMAXRTX: Maximum number of retransmissions of SYN segments.
 */
#define TCP_SYNMAXRTX                   3

/**
 * TCP_MAXRTX: Maximum number of retransmissions of data segments.
 */
#define TCP_MAXRTX                      8    // Match the default value of the comparison device

/* The maximum segment lifetime in milliseconds */
#define TCP_MSL                         5500UL

/**
 * LWIP_TCP_RTO_TIME: Initial TCP retransmission timeout (ms).
 * This defaults to 3 seconds as traditionally defined in the TCP protocol.
 * For improving timely recovery on faster networks, this value could
 * be lowered down to 1 second (RFC 6298)
 */
#define LWIP_TCP_RTO_TIME               3000

/**
 * LWIP_HOOK_TCP_ISN:
 * Hook for generation of the Initial Sequence Number (ISN) for a new TCP
 * connection. The default lwIP ISN generation algorithm is very basic and may
 * allow for TCP spoofing attacks. This hook provides the means to implement
 * the standardized ISN generation algorithm from RFC 6528 (see contrib/adons/tcp_isn),
 * or any other desired algorithm as a replacement.
 * Called from tcp_connect() and tcp_listen_input() when an ISN is needed for
 * a new TCP connection, if TCP support (@ref LWIP_TCP) is enabled.\n
 * Signature:\code{.c}
 * u32_t my_hook_tcp_isn(const ip_addr_t* local_ip, u16_t local_port, const ip_addr_t* remote_ip, u16_t remote_port);
 * \endcode
 * - it may be necessary to use "struct ip_addr" (ip4_addr, ip6_addr) instead of "ip_addr_t" in function declarations\n
 * Arguments:
 * - local_ip: pointer to the local IP address of the connection
 * - local_port: local port number of the connection (host-byte order)
 * - remote_ip: pointer to the remote IP address of the connection
 * - remote_port: remote port number of the connection (host-byte order)\n
 * Return value:
 * - the 32-bit Initial Sequence Number to use for the new TCP connection.
 */
/* Simulator modification: disabled; use lwip's built-in ISN counter */
/* #define LWIP_HOOK_TCP_ISN(local_ip, local_port, remote_ip, remote_port) xy_lwip_tcp_isn(local_ip, local_port, remote_ip, remote_port) */

/**
 * LWIP_TCP_KEEPALIVE==1: Enable TCP_KEEPIDLE, TCP_KEEPINTVL and TCP_KEEPCNT
 * options processing. Note that TCP_KEEPIDLE and TCP_KEEPINTVL have to be set
 * in seconds. (does not require sockets.c, and will affect tcp.c)
 */
#define LWIP_TCP_KEEPALIVE              1

/*
   ---------------------------------
   ---------- UDP options ----------
   ---------------------------------
*/
/**
 * LWIP_UDP==1: Turn on UDP.
 */
#define LWIP_UDP                        1

/**
 * UDP_TTL: Default Time-To-Live value.
 */
#define UDP_TTL                         255


/*
   ------------------------------------
   ----------- Core locking -----------
   ------------------------------------
*/
/**
 * LWIP_MPU_COMPATIBLE: enables special memory management mechanism
 * which makes lwip able to work on MPU (Memory Protection Unit) system
 * by not passing stack-pointers to other threads
 * (this decreases performance as memory is allocated from pools instead
 * of keeping it on the stack)
 */
#define LWIP_MPU_COMPATIBLE             0

/**
 * SYS_LIGHTWEIGHT_PROT==1: if you want inter-task protection for certain
 * critical regions during buffer allocation, deallocation and memory
 * allocation and deallocation.
 */
#define SYS_LIGHTWEIGHT_PROT            1

/**
 * LWIP_TCPIP_CORE_LOCKING_INPUT: when LWIP_TCPIP_CORE_LOCKING is enabled,
 * this lets tcpip_input() grab the mutex for input packets as well,
 * instead of allocating a message and passing it to tcpip_thread.
 *
 * ATTENTION: this does not work when tcpip_input() is called from
 * interrupt context!
 */
#define LWIP_TCPIP_CORE_LOCKING_INPUT   1


 /*
   ------------------------------------
   -------------- NO SYS --------------
   ------------------------------------
*/
/**
 * NO_SYS==1: Use lwIP without OS-awareness (no thread, semaphores, mutexes or
 * mboxes). This means threaded APIs cannot be used (socket, netconn,
 * i.e. everything in the 'api' folder), only the callback-style raw API is
 * available (and you have to watch out for yourself that you don't access
 * lwIP functions/structures from more than one context at a time!)
 */
#define NO_SYS                          0

/**
 * NO_SYS_NO_TIMERS==1: Drop support for sys_timeout when NO_SYS==1
 * Mainly for compatibility to old versions.
 */
#define NO_SYS_NO_TIMERS                1


/*
   ------------------------------------
   ---------- Memory options ----------
   ------------------------------------
*/
/**
 * MEM_ALIGNMENT: should be set to the alignment of the CPU
 *    4 byte alignment -> \#define MEM_ALIGNMENT 4
 *    2 byte alignment -> \#define MEM_ALIGNMENT 2
 */
#define MEM_ALIGNMENT                   4

/**
 * MEM_SIZE: the size of the heap memory. If the application will send
 * a lot of data that needs to be copied, this should be set high.
 */
#define MEM_SIZE                        (10*1024)

/* in case C library malloc() needs extra protection,
 * allow these defines to be overridden.
 */
#define mem_clib_free                   xy_free

/* in case C library malloc() needs extra protection,
 * allow these defines to be overridden.
 */
#define mem_clib_malloc                 xy_malloc


/*
   ------------------------------------------------
   ---------- Internal Memory Pool Sizes ----------
   ------------------------------------------------
*/
/**
 * MEMP_NUM_PBUF: the number of memp struct pbufs (used for PBUF_ROM and PBUF_REF).
 * If the application sends a lot of data out of ROM (or other static memory),
 * this should be set high.
 */
#define MEMP_NUM_PBUF                   20

/**
 * MEMP_NUM_UDP_PCB: the number of UDP protocol control blocks. One
 * per active UDP "connection".
 * (requires the LWIP_UDP option)
 */
#define MEMP_NUM_UDP_PCB                10

/**
 * MEMP_NUM_TCP_PCB: the number of simultaneously active TCP connections.
 * (requires the LWIP_TCP option)
 */
#define MEMP_NUM_TCP_PCB                10

/**
 * MEMP_NUM_TCP_PCB_LISTEN: the number of listening TCP connections.
 * (requires the LWIP_TCP option)
 */
#define MEMP_NUM_TCP_PCB_LISTEN         0

/**
 * MEMP_NUM_TCP_SEG: the number of simultaneously queued TCP segments.
 * (requires the LWIP_TCP option)
 */
#define MEMP_NUM_TCP_SEG                12

/**
 * MEMP_NUM_SYS_TIMEOUT: the number of simultaneously active timeouts.
 * The default number of timeouts is calculated here for all enabled modules.
 * The formula expects settings to be either '0' or '1'.
 */
#define MEMP_NUM_SYS_TIMEOUT            12

/**
 * MEMP_NUM_NETCONN: the number of struct netconns.
 * (only needed if you use the sequential API, like api_lib.c)
 */
#define MEMP_NUM_NETCONN                22

/**
 * MEM_LIBC_MALLOC==1: Use malloc/free/realloc provided by your C-library
 * instead of the lwip internal allocator. Can save code size if you
 * already use it.
 */
#define MEM_LIBC_MALLOC                 1

/**
 * MEMP_MEM_MALLOC==1: Use mem_malloc/mem_free instead of the lwip pool allocator.
 * Especially useful with MEM_LIBC_MALLOC but handle with care regarding execution
 * speed (heap alloc can be much slower than pool alloc) and usage from interrupts
 * (especially if your netif driver allocates PBUF_POOL pbufs for received frames
 * from interrupt)!
 * ATTENTION: Currently, this uses the heap for ALL pools (also for private pools,
 * not only for internal pools defined in memp_std.h)!
 */
#define MEMP_MEM_MALLOC                 1

/**
 * PBUF_POOL_SIZE: the number of buffers in the pbuf pool.
 */
#define PBUF_POOL_SIZE                  20

/**
 * PBUF_POOL_BUFSIZE: the size of each pbuf in the pbuf pool. The default is
 * designed to accommodate single full size TCP frame in one pbuf, including
 * TCP_MSS, IP header, and link header.
 */
#define PBUF_POOL_BUFSIZE               500

/**
 * MEMP_NUM_NETBUF: the number of struct netbufs.
 * (only needed if you use the sequential API, like api_lib.c)
 */
#define MEMP_NUM_NETBUF                 20

/**
 * MEMP_NUM_TCPIP_MSG_INPKT: the number of struct tcpip_msg, which are used
 * for incoming packets.
 * (only needed if you use tcpip.c)
 */
#define MEMP_NUM_TCPIP_MSG_INPKT        12

/**
 * MEMP_NUM_SNTP_MSG: the number of struct sntp_api_msg, which are used
 * for sntp msg.
 */
#define MEMP_NUM_SNTP_MSG               1


/*
   ----------------------------------
   ---------- ICMP options ----------
   ----------------------------------
*/
/**
 * LWIP_ICMP==1: Enable ICMP module inside the IP stack.
 * Be careful, disable that make your product non-compliant to RFC1122
 */
#define LWIP_ICMP                       1


/*
   ----------------------------------
   ---------- DHCP options ----------
   ----------------------------------
*/
/**
 * LWIP_DHCP==1: Enable DHCP module.
 */
#define LWIP_DHCP                       0


/*
   ----------------------------------------
   ---------- Statistics options ----------
   ----------------------------------------
*/
/**
 * LWIP_STATS==1: Enable statistics collection in lwip_stats.
 */
#define LWIP_STATS                      0

#define LINK_STATS                      0

#define ETHARP_STATS                    0

#define IPFRAG_STATS                    0

#define IP_STATS                        1

#define ICMP_STATS                      0

#define IGMP_STATS                      0

#define UDP_STATS                       1

#define TCP_STATS                       1

#define MEM_STATS                       0

#define MEMP_STATS                      0

#define SYS_STATS                       0

#define IP6_STATS                       1

#define ICMP6_STATS                     0

#define IP6_FRAG_STATS                  0

#define MLD6_STATS                      0

#define ND6_STATS                       0

#define MIB2_STATS                      0

/** LWIP_PROVIDE_ERRNO==1: Let lwIP provide ERRNO values and the 'errno' variable.
 * If this is disabled, cc.h must either define 'errno', include <errno.h>,
 * define LWIP_ERRNO_STDINCLUDE to get <errno.h> included or
 * define LWIP_ERRNO_INCLUDE to <errno.h> or equivalent.
 */
#define LWIP_ERRNO_STDINCLUDE           1


/*
   ------------------------------------------------
   ---------- Network Interfaces options ----------
   ------------------------------------------------
*/
/**
 * LWIP_NETIF_LINK_CALLBACK==1: Support a callback function from an interface
 * whenever the link changes (i.e., link down)
 */
#define LWIP_NETIF_LINK_CALLBACK        1

/**
 * LWIP_NETIF_STATUS_CALLBACK==1: Support a callback function whenever an interface
 * changes its up/down status (i.e., due to DHCP IP acquisition)
 */
#define LWIP_NETIF_STATUS_CALLBACK      1

/**
 * LWIP_NETIF_API==1: Support netif api (in netifapi.c)
 */
#define LWIP_NETIF_API                  1

/**
 * LWIP_NETIF_LOOPBACK==1: Support sending packets with a destination IP
 * address equal to the netif IP address, looping them back up the stack.
 */
#define LWIP_NETIF_LOOPBACK             1

/**
 * LWIP_HAVE_LOOPIF==1: Support loop interface (127.0.0.1).
 * This is only needed when no real netifs are available. If at least one other
 * netif is available, loopback traffic uses this netif.
 */
#define LWIP_HAVE_LOOPIF                1


/*
   --------------------------------------
   ---------- Checksum options ----------
   --------------------------------------
*/
/**
 * LWIP_CHECKSUM_ON_COPY==1: Calculate checksum when copying data from
 * application buffers to pbufs.
 */
#define LWIP_CHECKSUM_ON_COPY               1


/*
   ------------------------------------
   ---------- Socket options ----------
   ------------------------------------
*/
/**
 * LWIP_SOCKET==1: Enable Socket API (require to use sockets.c)
 */
#define LWIP_SOCKET                         1

/**
 * LWIP_COMPAT_SOCKETS==1: Enable BSD-style sockets functions names through defines.
 * LWIP_COMPAT_SOCKETS==2: Same as ==1 but correctly named functions are created.
 * While this helps code completion, it might conflict with existing libraries.
 * (only used if you use sockets.c)
 */
#define LWIP_COMPAT_SOCKETS                 2

/**
 * LWIP_SO_RCVTIMEO==1: Enable receive timeout for sockets/netconns and
 * SO_RCVTIMEO processing.
 */
#define LWIP_SO_RCVTIMEO                    1

/**
 * LWIP_SO_RCVBUF==1: Enable SO_RCVBUF processing.
 */
#define LWIP_SO_RCVBUF                      1

/**
 * LWIP_SO_LINGER==1: Enable SO_LINGER processing.
 */
#define LWIP_SO_LINGER                      1

/**
 * SO_REUSE==1: Enable SO_REUSEADDR option.
 */
#define SO_REUSE                            1


/*
   ----------------------------------------------
   ---------- Sequential layer options ----------
   ----------------------------------------------
*/
/**
 * LWIP_NETCONN==1: Enable Netconn API (require to use api_lib.c)
 */
#define LWIP_NETCONN                        1

/** LWIP_NETCONN_FULLDUPLEX==1: Enable code that allows reading from one thread,
 * writing from a 2nd thread and closing from a 3rd thread at the same time.
 * LWIP_NETCONN_SEM_PER_THREAD==1 is required to use one socket/netconn from
 * multiple threads at once!
 */
#define LWIP_NETCONN_FULLDUPLEX             1


/*
   ---------------------------------
   ---------- RAW options ----------
   ---------------------------------
*/
/**
 * LWIP_RAW==1: Enable application layer to hook into the IP layer itself.
 */
#define LWIP_RAW                            1


/*
   ------------------------------------
   ---------- Thread options ----------
   ------------------------------------
*/
/**
 * TCPIP_THREAD_STACKSIZE: The stack size used by the main tcpip thread.
 * The stack size value itself is platform-dependent, but is passed to
 * sys_thread_new() when the thread is created.
 */
#define TCPIP_THREAD_STACKSIZE              0xC00

/**
 * TCPIP_MBOX_SIZE: The mailbox size for the tcpip thread messages
 * The queue size value itself is platform-dependent, but is passed to
 * sys_mbox_new() when tcpip_init is called.
 */
#define TCPIP_MBOX_SIZE                     12

/**
 * TCPIP_THREAD_PRIO: The priority assigned to the main tcpip thread.
 * The priority value itself is platform-dependent, but is passed to
 * sys_thread_new() when the thread is created.
 */
#define TCPIP_THREAD_PRIO                   osPriorityAboveNormal

/**
 * TCPIP_THREAD_NAME: The name assigned to the main tcpip thread.
 */
#define TCPIP_THREAD_NAME                   "tcpip_thd"

/**
 * DEFAULT_UDP_RECVMBOX_SIZE: The mailbox size for the incoming packets on a
 * NETCONN_UDP. The queue size value itself is platform-dependent, but is passed
 * to sys_mbox_new() when the recvmbox is created.
 */
#define DEFAULT_UDP_RECVMBOX_SIZE           64

/**
 * DEFAULT_TCP_RECVMBOX_SIZE: The mailbox size for the incoming packets on a
 * NETCONN_TCP. The queue size value itself is platform-dependent, but is passed
 * to sys_mbox_new() when the recvmbox is created.
 */
#define DEFAULT_TCP_RECVMBOX_SIZE           64

/**
 * DEFAULT_ACCEPTMBOX_SIZE: The mailbox size for the incoming connections.
 * The queue size value itself is platform-dependent, but is passed to
 * sys_mbox_new() when the acceptmbox is created.
 */
#define DEFAULT_ACCEPTMBOX_SIZE             6

/**
 * DEFAULT_RAW_RECVMBOX_SIZE: The mailbox size for the incoming packets on a
 * NETCONN_RAW. The queue size value itself is platform-dependent, but is passed
 * to sys_mbox_new() when the recvmbox is created.
 */
#define DEFAULT_RAW_RECVMBOX_SIZE           6

/** LWIP_TCPIP_TIMEOUT==1: Enable tcpip_timeout/tcpip_untimeout to create
 * timers running in tcpip_thread from another thread.
 */
#define LWIP_TCPIP_TIMEOUT                  1


/*
   ---------------------------------------
   ---------- IPv6 options ---------------
   ---------------------------------------
*/
/**
 * LWIP_IPV6==1: Enable IPv6
 */
/* Simulator modification: once tried setting this to 0; restored to 1 due to
 * the SDK's dual-stack patch dependency (see the note at the top of this file) */
#define LWIP_IPV6                           1
/**
 * LWIP_ND6_NUM_NEIGHBORS: Number of entries in IPv6 neighbor cache
 */
#define LWIP_ND6_NUM_NEIGHBORS              2

/**
 * LWIP_ND6_NUM_DESTINATIONS: number of entries in IPv6 destination cache
 */
#define LWIP_ND6_NUM_DESTINATIONS           2

/**
 * LWIP_ND6_NUM_PREFIXES: number of entries in IPv6 on-link prefixes cache
 */
#define LWIP_ND6_NUM_PREFIXES               2

/**
 * LWIP_ND6_NUM_ROUTERS: number of entries in IPv6 default router cache
 */
#define LWIP_ND6_NUM_ROUTERS                0

/**
 * MEMP_NUM_ND6_QUEUE: Max number of IPv6 packets to queue during MAC resolution.
 */
#define MEMP_NUM_ND6_QUEUE                  4

/**
 * LWIP_IPV6_NUM_ADDRESSES: Number of IPv6 addresses per netif.
 */
#define LWIP_IPV6_NUM_ADDRESSES             2

/**
 * LWIP_IPV6_SEND_ROUTER_SOLICIT==1: Send router solicitation messages during
 * network startup.
 */
#define LWIP_IPV6_SEND_ROUTER_SOLICIT       1

/**
 * LWIP_IPV6_DUP_DETECT_ATTEMPTS=[0..7]: Number of duplicate address detection attempts.
 */
#define LWIP_IPV6_DUP_DETECT_ATTEMPTS       0

/**
 * LWIP_IPV6_MLD==1: Enable multicast listener discovery protocol.
 * If LWIP_IPV6 is enabled but this setting is disabled, the MAC layer must
 * indiscriminately pass all inbound IPv6 multicast traffic to lwIP.
 */
#define LWIP_IPV6_MLD                       1

/**
 * LWIP_ND6_TCP_REACHABILITY_HINTS==1: Allow TCP to provide Neighbor Discovery
 * with reachability hints for connected destinations. This helps avoid sending
 * unicast neighbor solicitation messages.
 */
#define LWIP_ND6_TCP_REACHABILITY_HINTS     0

/**
 * LWIP_IPV6_FORWARD==1: Forward IPv6 packets across netifs
 */
#define LWIP_IPV6_FORWARD                    1

/**
 * IP_ROUTE==1: Support IPv6 route.
 */
#define IP6_ROUTE                            1

/**
 * LWIP_HOOK_IP6_ROUTE(src, dest):
 * Called from ip_route() (IPv6)
 * Signature:\code{.c}
 *   struct netif *my_hook(const ip6_addr_t *dest, const ip6_addr_t *src);
 * \endcode
 * Arguments:
 * - src: source IPv6 address
 * - dest: destination IPv6 address
 * Return values:
 * - the destination netif
 * - NULL if no destination netif is found. In that case, ip6_route() continues as normal.
 */
/* Simulator modification: hook disabled (LWIP_IPV6 itself stays 1, see note 4 above) */
/* #define LWIP_HOOK_IP6_ROUTE                 ip6_static_route */

/** Some defines for DHCP to let link-layer-addressed packets through while the
 * netif is down.
 * To use this in your own application/protocol, define LWIP_IP_ACCEPT_UDP_PORT(port)
 * to return 1 if the port is accepted and 0 if the port is not accepted.
 */
#define LWIP_IP_ACCEPT_UDP_PORT(port)       ((port) == PP_NTOHS(LWIP_IANA_PORT_DHCP_SERVER))

/** IP6_FRAG_COPYHEADER==1: for platforms where sizeof(void*) > 4, "struct
 * ip6_reass_helper" is too large to be stored in the IPv6 fragment header, and
 * will bleed into the header before it, which may be the IPv6 header or an
 * extension header. This means that for each first fragment packet, we need to
 * 1) make a copy of some IPv6 header fields (src+dest) that we need later on,
 * just in case we do overwrite part of the IPv6 header, and 2) make a copy of
 * the header data that we overwrote, so that we can restore it before either
 * completing reassembly or sending an ICMPv6 reply. The last part is true even
 * if this setting is disabled, but if it is enabled, we need to save a bit
 * more data (up to the size of a pointer) because we overwrite more. 
 */
#define IPV6_FRAG_COPYHEADER                1


/*
   ----------------------------------
   ---------- DNS options -----------
   ----------------------------------
*/
/**
 * LWIP_DNS==1: Turn on DNS module. UDP must be available for DNS
 * transport.
 */
#define LWIP_DNS                            1

/** The maximum of DNS servers
 * The first server can be initialized automatically by defining
 * DNS_SERVER_ADDRESS(ipaddr), where 'ipaddr' is an 'ip_addr_t*'
 */
#define DNS_MAX_SERVERS                     4

/** DNS maximum number of entries to maintain locally. */
#define DNS_TABLE_SIZE                      4

/** DNS maximum host name length supported in the name table. */
#define DNS_MAX_NAME_LENGTH                 256

/** DNS maximum number of retries when asking for a name, before "timeout". */
#define DNS_MAX_RETRIES                     2


/*
   ---------------------------------------
   ---------- Sntp options ---------------
   ---------------------------------------
*/
/**
 * LWIP_SNTP==1: Turn on SNTP module. 
 */
#define LWIP_SNTP                           1

/** 
 * Set this to 1 to support DNS names (or IP address strings) to set sntp servers
 * One server address/name can be defined as default if SNTP_SERVER_DNS == 1:
 */
#define SNTP_SERVER_DNS                     1

/** Enable round-trip delay compensation.
 * Compensate for the round-trip delay by calculating the clock offset from
 * the originate, receive, transmit and destination timestamps, as per RFC.
 *
 * The calculation requires compiler support for 64-bit integers. Also, either
 * SNTP_SET_SYSTEM_TIME_US or SNTP_SET_SYSTEM_TIME_NTP has to be implemented
 * for setting the system clock with sub-second precision. Likewise, either
 * SNTP_GET_SYSTEM_TIME or SNTP_GET_SYSTEM_TIME_NTP needs to be implemented
 * with sub-second precision.
 *
 * Although not strictly required, it makes sense to combine this option with
 * SNTP_CHECK_RESPONSE >= 2 for sanity-checking of the received timestamps.
 * Also, in order for the round-trip calculation to work, the difference
 * between the local clock and the NTP server clock must not be larger than
 * about 34 years. If that limit is exceeded, the implementation will fall back
 * to setting the clock without compensation. In order to ensure that the local
 * clock is always within the permitted range for compensation, even at first
 * try, it may be necessary to store at least the current year in non-volatile
 * memory.
 */
#define SNTP_CHECK_RESPONSE                 1

/** SNTP update delay - in milliseconds
 * Default is 1 hour. Must not be beolw 60 seconds by specification (i.e. 60000)
 */
#define SNTP_UPDATE_DELAY                   60000

/** According to the RFC, this shall be a random delay
 * between 1 and 5 minutes (in milliseconds) to prevent load peaks.
 * This can be defined to a random generation function,
 * which must return the delay in milliseconds as u32_t.
 * Turned off by default.
 */
#define SNTP_STARTUP_DELAY                  0

/** SNTP maximum number of retries, before "timeout". */
#define SNTP_MAX_RETRIES                    1

/** If you want the startup delay to be a function, define this
 * to a function (including the brackets) and define SNTP_STARTUP_DELAY to 1.
 */
#define SNTP_STARTUP_DELAY_FUNC             20000

/*
   ----------------------------------------
   ---------- PPP options ----------
   ----------------------------------------
*/
/**
 * PPP_SUPPORT==1: Enable PPP.
 */
#define PPP_SUPPORT                          XY_PPP

/**
 * PPPOS_SUPPORT==1: Enable PPP Over Serial
 */
#define PPPOS_SUPPORT                        1

/**
 * PPP_SERVER==1: Enable PPP server support (waiting for incoming PPP session).
 *
 * Currently only supported for PPPoS.
 */
#define PPP_SERVER                           1

/**
 * PAP_SUPPORT==1: Support PAP.
 */
#define PAP_SUPPORT                          1

/**
 * CHAP_SUPPORT==1: Support CHAP.
 */
#define CHAP_SUPPORT                         1

/**
 * EAP_SUPPORT==1: Support EAP.
 */
#define EAP_SUPPORT                          1

/**
 * PPP_IPV4_SUPPORT==1: Enable PPP IPv4 support
 */
#define PPP_IPV4_SUPPORT                     1

/**
 * PPP_IPV4_SUPPORT==0: Disable PPP IPv4 support
 */
#define PPP_IPV6_SUPPORT                     1

/**
 * PPP_NOTIFY_PHASE==1: Support PPP notify phase support
 */
#define PPP_NOTIFY_PHASE                     1
 
/** 
 * CCP_SUPPORT==1: Support CCP.
 */
#define CCP_SUPPORT                          0

/**
 * VJ_SUPPORT==1: Support VJ header compression.
 */
#define VJ_SUPPORT                           0

/**
 * LWIP_USE_EXTERNAL_MBEDTLS: Use external mbed TLS library
 */
#define LWIP_USE_EXTERNAL_MBEDTLS            1

/*
   ----------------------------------------
   --------- Lwip timeouts options --------
   ----------------------------------------
*/
/**
 * LWIP_TESTMODE: Changes to make unit test possible
 */
#define LWIP_TESTMODE                        1

/*
   ----------------------------------------
   ---------- Lwip Debug options ----------
   ----------------------------------------
*/
/* Troubleshooting note: to enable = uncomment the line below and add
 *     #define LWIP_PLATFORM_DIAG(x)   do { sim_lwip_diag x; } while (0)
 *     (sim_lwip_diag is in sim-sdk-common/src/sim_xylog.c, goes through the
 *     simulation log ring)
 *   then add the module levels you need: DHCP_DEBUG/UDP_DEBUG/IP_DEBUG/
 *     NETIF_DEBUG/TIMERS_DEBUG/TCPIP_DEBUG = LWIP_DBG_ON
 *   lwipopts is a global override header; after changing it you must do a
 *   clean rebuild.
 *   On 2026-09-05 this is how we tracked down the DHCP yiaddr 4-byte offset
 *   bug. */
//#define LWIP_DEBUG

/*
   ---------------------------------------------------
   -------------- Xinyi Custom options ---------------
   ---------------------------------------------------
*/
/**
 * DNS_NAME_ALLOC_IS_DYNAMIC==1: DNS domain names are stored as pointers to save memory
 */ 
#define DNS_NAME_ALLOC_IS_DYNAMIC           1

/**
 * DNS_MAX_ADDR_ANSWER>1: Support obtaining multiple IP addresses after DNS domain name resolution, as well as the TTL of the last IP address
 */ 
#define DNS_MAX_ADDR_ANSWER                 4

/**
 * LWIP_XY_CUSTOM==1: Enable Xinyi customization of LWIP source code
 */ 
#define LWIP_XY_CUSTOM                      1

/**
 * LWIP_XY_LPM_CUSTOM==1: Enable Xinyi LWIP LPM customization
 */ 
#define LWIP_XY_LPM_CUSTOM                  1

/**
 * LWIP_DBG_PRINTF==1: Enable printf for LWIP_ASSERT and LWIP_ERROR
 */
#define LWIP_DBG_PRINTF                     0

/**
 *  Default mtu value
 */
#define LWIP_DEFAULT_MTU_LENGTH             1500


#endif /* __LWIPOPTS_H__ */
/************************ (C) COPYRIGHT XINYISEMI *********/
