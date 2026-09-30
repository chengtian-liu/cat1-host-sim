# sim-sdk-common -- common layer shared by the SDK family
#
# This directory carries two build responsibilities:
#
#   1. SDK libraries (xyos / xyat / xynet / xymbed): build the real SDK
#      sources (FreeRTOS + at_ctrl + lwIP + mbedtls, all referenced
#      read-only) into static libraries.
#      These library definitions are an "SDK family" property -- other chip
#      products in the same family reuse them directly; chip differences are
#      injected via just two variables (provided by sim-target.cmake in the
#      target directory):
#        SIM_CHIP_MEMMAP   chip memory-layout directory name (memlayout/<SIM_CHIP_MEMMAP>)
#
#   2. simcommon static library: host stubs common to the SDK family
#      (FreeRTOS/lwIP/AT framework shims, fake CP, virtual serial devices,
#      FLASH/RTC/ICC stubs, pcap, log bridge).
#      These stubs depend on SDK headers but not on any specific chip; all
#      products share them.
#
# Dependencies: simcore (common simulation core, pure Win32).
#
# [!] Header shadowing order (must not be reordered): shadow directories
#   (target include/include_host, this layer's include/include_host/root,
#   sim-core/include) must come before the SDK directories, so that -I
#   ordering makes the simulator versions of FreeRTOSConfig.h / hw_types.h /
#   lwipopts.h / memmap.h etc. shadow the SDK originals. CMake rule: when
#   compiling a library, that library's own includes are emitted first, then
#   dependency libraries' INTERFACE includes are appended in link order --
#   each library's own list is already arranged accordingly.

set(SIM_SDK_COMMON ${CMAKE_CURRENT_LIST_DIR})

if(NOT DEFINED SIM_CHIP_MEMMAP)
    message(FATAL_ERROR
        "SIM_CHIP_MEMMAP is not defined. The target directory must provide sim-target.cmake, "
        "set(SIM_CHIP_MEMMAP <memlayout subdirectory name>) in it, "
        "and the top-level CMakeLists must include it before add_subdirectory(sim-sdk-common).")
endif()

set(FR_OS    ${SDK_ROOT}/platform/kernel/os/FreeRTOS)
set(OSAL     ${SDK_ROOT}/platform/kernel/osal/FreeRTOS)
set(WINPORT  ${CMAKE_SOURCE_DIR}/third_party/FreeRTOS-Kernel/portable/MSVC-MingW)

# SDK feature-module switches (family-wide; products needing to trim can make them CACHE variables)
set(ENABLE_AT_FS_QUEC  1)
set(ENABLE_AT_SSL_QUEC 1)
set(ENABLE_AT_HTTP_QUEC 1)
set(ENABLE_AT_FTP_QUEC 1)
set(ENABLE_AT_MQTT_QUEC 1)

# ---------------------------------------------------------------------------
# xyos -- real OS layer (SDK original FreeRTOS + cmsis_os2/osal + host stubs)
# ---------------------------------------------------------------------------
add_library(xyos STATIC
    # --- SDK kernel (read-only, not modified) ---
    ${FR_OS}/tasks.c
    ${FR_OS}/queue.c
    ${FR_OS}/timers.c
    ${FR_OS}/list.c
    ${FR_OS}/event_groups.c
    ${FR_OS}/stream_buffer.c
    # --- SDK cmsis_os2 adaptation layer (read-only) ---
    ${OSAL}/Source/osal.c
    ${OSAL}/Source/osal_statistics.c
    ${OSAL}/Source/osal_cpu_utilization.c
    ${OSAL}/Source/osal_cpu_using.c
    ${OSAL}/Source/osal_freertos_hook.c
    # --- Windows port (official MSVC-MingW) ---
    ${WINPORT}/port.c
    # --- Host stubs (heap/generic peripherals, common to the SDK family) ---
    ${SIM_SDK_COMMON}/src/sim_shims.c
    # --- Factory NV (defaults + g_softap_fac_nv; target overrides via factory_nv_init) ---
    ${SIM_SDK_COMMON}/src/sim_factory_nv.c
    # --- XY log stub ---
    ${SIM_SDK_COMMON}/src/sim_xylog.c
)

target_include_directories(xyos PUBLIC
    # shadow directories (chip-specific first, family-wide last); must precede all SDK directories
    ${SIM_TARGET_DIR}/include
    ${SIM_TARGET_DIR}/include_host
    ${SIM_SDK_COMMON}/include
    ${SIM_SDK_COMMON}/include_host
    ${CMAKE_SOURCE_DIR}/sim-core/include
    # SDK directories
    ${FR_OS}/include
    ${FR_OS}/extend
    ${WINPORT}
    ${OSAL}/Include
    ${OSAL}/Config
    ${SDK_ROOT}/platform/kernel/misc/inc
    ${SDK_ROOT}/platform/include
    ${SDK_ROOT}/platform/kernel/appstart/inc
    ${SDK_ROOT}/platform/drivers/driverlib/inc
)

target_link_libraries(xyos PUBLIC winmm)
target_compile_options(xyos PRIVATE -Wno-unused-parameter -fno-strict-aliasing)

# ---------------------------------------------------------------------------
# xyat -- real AT framework (SDK at_ctrl + posix + PPP)
# ---------------------------------------------------------------------------
set(AT_CTRL ${SDK_ROOT}/platform/application/at_ctrl)
set(AT_CMD  ${SDK_ROOT}/platform/application/at_cmd)
set(POSIX   ${SDK_ROOT}/platform/kernel/posix)
set(PPP     ${SDK_ROOT}/platform/application/ppp)

add_library(xyat STATIC
    # --- All 14 at_ctrl files ---
    ${AT_CTRL}/src/at_api.c
    ${AT_CTRL}/src/at_com.c
    ${AT_CTRL}/src/at_context.c
    ${AT_CTRL}/src/at_ctl.c
    ${AT_CTRL}/src/at_ctl_basic.c
    ${AT_CTRL}/src/at_error.c
    ${AT_CTRL}/src/at_farps_channel.c
    ${AT_CTRL}/src/at_passthrough.c
    ${AT_CTRL}/src/at_proxy.c
    ${AT_CTRL}/src/at_recv.c
    ${AT_CTRL}/src/at_response.c
    ${AT_CTRL}/src/at_utils.c
    ${AT_CTRL}/src/ps_adapt.c
    ${AT_CTRL}/src/sms_passthrough.c
    # --- posix layer ---
    ${POSIX}/src/posix_io.c
    ${POSIX}/src/posix_port.c
    ${POSIX}/device/src/null_device.c
    # --- PPP ---
    ${PPP}/src/at_ppp.c
    # --- Chip memory-layout constants (chip difference point, injected via SIM_CHIP_MEMMAP) ---
    ${SDK_ROOT}/memlayout/${SIM_CHIP_MEMMAP}/memmap.c
    # --- CMUX (with XY_CMUX=1, at_context.h -> cmux.h; tightly coupled to the AT framework) ---
    ${SDK_ROOT}/platform/application/cmux/core/src/cmux.c
    ${SDK_ROOT}/platform/application/cmux/core/src/cmux_utils.c
    ${SDK_ROOT}/platform/application/cmux/at_cmux/src/at_cmux.c
)

target_include_directories(xyat PUBLIC
    # shadow (host-side USB/hw_types shims) before SDK directories
    ${SIM_TARGET_DIR}/include_host
    ${SIM_SDK_COMMON}/include_host
    ${AT_CTRL}/inc
    ${AT_CMD}/inc
    ${POSIX}/inc
    ${POSIX}/device/inc
    ${SDK_ROOT}/platform/kernel/inter_core/channel/inc
    ${SDK_ROOT}/platform/kernel/inter_core/inc
    ${SDK_ROOT}/platform/LtePs/atc_ps_cmd/inc
    ${SDK_ROOT}/platform/LtePs/atc_ps_cmd/atc_ps/inc
    ${SDK_ROOT}/platform/LtePs/atc_ps_cmd/pack_tool/Inc
    ${SDK_ROOT}/platform/LtePs/ps_external/inc
    ${SDK_ROOT}/platform/kernel/flash_daemon/inc
    ${SDK_ROOT}/platform/application/lock/inc
    ${SDK_ROOT}/platform/application/fs/xy_fs/inc
    ${SDK_ROOT}/platform/application/fs/littlefs
    ${SDK_ROOT}/platform/application/common/inc
    ${SDK_ROOT}/platform/application/xy_version
    ${SDK_ROOT}/platform/application/network/net_adapt/inc
    ${SDK_ROOT}/platform/application/network/ril/inc
    ${SDK_ROOT}/platform/application/netdata/netpkt/inc
    ${SDK_ROOT}/platform/application/network/lwip/lwip-2.1.3/include
    ${SDK_ROOT}/platform/application/network/lwip/lwip-2.1.3/lwip_config
    ${SDK_ROOT}/platform/application/network/lwip/lwip-2.1.3/lwip_port
    ${PPP}/inc
    ${SDK_ROOT}/platform/kernel/usbx/usb_api/inc
    ${SDK_ROOT}/platform/kernel/usbx/core/inc
    ${SDK_ROOT}/platform/kernel/usbx/ports/inc
    ${SDK_ROOT}/platform/kernel/lowpower/inc
    ${SDK_ROOT}/platform/kernel/special/ip_log/inc
    ${SDK_ROOT}/platform/drivers/driverlib/port/inc
    ${SDK_ROOT}/platform/arch/lib/common/inc
    ${SDK_ROOT}
    ${SDK_ROOT}/platform/application/cmux/core/inc
    ${SDK_ROOT}/platform/application/cmux/at_cmux/inc
)

target_compile_definitions(xyat PUBLIC
    WIN32_LEAN_AND_MEAN
    CONFIG_BOARD_MEMMAP="memlayout/${SIM_CHIP_MEMMAP}/memmap.h"
    UX_STANDALONE=1
    LPUART_AT=1
    UART_AT=1
    USB_DEVICE=1
    USB_AT=1
    USB_LOG=1
    CSP_LOG=1
    MAIN_UART_AT_DEV=1
    RIL_AT=1
    LOG_AT=0
    USB_MODEM=0
    XY_CMUX=1
    PS_TEST_MODE=0
    AT_TEST_OFF=0
    AT_AUDIO=0
    WATER_MTR_AI=0
    XY_GNSS1=0
    XY_GNSS2=0
    TLSF_HARDWARE=1
    VER_CM=0
    VER_NB=0
    VER_QUEC=1
    AT_BASE=1
    AT_BASE_QUEC=1
    AT_BASE_CMCC=0
    AT_BASE_NB=0
    AT_PING_QUEC=1
    AT_PING_CMCC=0
    AT_NTP_QUEC=1
    AT_NTP_CMCC=0
    AT_SOCK_QUEC=1
    AT_TCPIP_QUEC=1
    AT_SOCK_CMCC=0
    AT_SOCK_NB=0
    AT_SSL_QUEC=${ENABLE_AT_SSL_QUEC}
    AT_SSL_CMCC=0
    MULTI_SUBNET=1
    # --- Disabled for simulation: sim_proxy handles TCP/DNS itself ---
    TCP_WND_OPTIMIZE=0
    DNS_RELAY=0
    XY_PPP=1
    CONFIG_RADVD=1
    DHCP6_SERVER=1
    AT_FS_QUEC=${ENABLE_AT_FS_QUEC}
    LFS_THREADSAFE=0
    AT_HTTP_QUEC=${ENABLE_AT_HTTP_QUEC}
    AT_HTTP_CMCC=0
    HTTP_SSL=1
    AT_FTP_QUEC=${ENABLE_AT_FTP_QUEC}
    AT_FTP_CMCC=0
    FTP_SSL=1
    AT_MQTT_QUEC=${ENABLE_AT_MQTT_QUEC}
    AT_MQTT_CMCC=0
    MQTT_SSL=1
    MQTT_TASK=0
    MBEDTLS_CONFIG_FILE=<mbedtls_config_ssl.h>
    MBEDTLS_ALLOW_PRIVATE_ACCESS=1
)

target_compile_options(xyat PRIVATE -Wno-unused-parameter -fno-strict-aliasing)
target_compile_options(xyat PRIVATE -include ${SIM_SDK_COMMON}/include/sim_missing_decls.h)
target_link_libraries(xyat PUBLIC xyos)

# ---------------------------------------------------------------------------
# xymbed -- mbedtls 3.6.4 (version bundled in the SDK tree, referenced read-only)
# ---------------------------------------------------------------------------
set(MBEDTLS ${SDK_ROOT}/platform/application/ssl/mbedtls-3.6.4)
file(GLOB XMBEDTLS_SRCS ${MBEDTLS}/library/*.c)

add_library(xymbed STATIC ${XMBEDTLS_SRCS})
target_include_directories(xymbed PUBLIC
    ${MBEDTLS}/include
    ${MBEDTLS}/include/mbedtls
    ${MBEDTLS}/library
    ${SDK_ROOT}/platform/application/ssl/xy_port/inc
    ${SDK_ROOT}/platform/application/fs/xy_fs/inc
    ${SDK_ROOT}/platform/application/fs/littlefs
    ${SDK_ROOT}/platform/application/common/inc
    ${SDK_ROOT}/platform/kernel/rtc/inc
)
target_compile_definitions(xymbed PUBLIC
    MBEDTLS_CONFIG_FILE=<mbedtls_config_ssl.h>
    MBEDTLS_ALLOW_PRIVATE_ACCESS=1
)
target_compile_options(xymbed PRIVATE -Wno-unused-parameter -fno-strict-aliasing)
target_compile_definitions(xymbed PRIVATE _WIN32_WINNT=0x0501)
target_link_libraries(xymbed PUBLIC xyos)

# ---------------------------------------------------------------------------
# xynet -- real lwip protocol stack (SDK original 2.1.3 + OS port + network stubs)
# ---------------------------------------------------------------------------
set(LWIP ${SDK_ROOT}/platform/application/network/lwip/lwip-2.1.3)

add_library(xynet STATIC
    # --- lwip api layer ---
    ${LWIP}/api/api_lib.c
    ${LWIP}/api/api_msg.c
    ${LWIP}/api/err.c
    ${LWIP}/api/if_api.c
    ${LWIP}/api/netbuf.c
    ${LWIP}/api/netdb.c
    ${LWIP}/api/netifapi.c
    ${LWIP}/api/sockets.c
    ${LWIP}/api/tcpip.c
    # --- lwip core ---
    ${LWIP}/core/def.c
    ${LWIP}/core/dns.c
    ${LWIP}/core/inet_chksum.c
    ${LWIP}/core/init.c
    ${LWIP}/core/ip.c
    ${LWIP}/core/mem.c
    ${LWIP}/core/memp.c
    ${LWIP}/core/netif.c
    ${LWIP}/core/pbuf.c
    ${LWIP}/core/raw.c
    ${LWIP}/core/stats.c
    ${LWIP}/core/sys.c
    ${LWIP}/core/tcp.c
    ${LWIP}/core/tcp_in.c
    ${LWIP}/core/tcp_out.c
    ${LWIP}/core/timeouts.c
    ${LWIP}/core/udp.c
    # --- lwip ipv4 ---
    ${LWIP}/core/ipv4/etharp.c
    ${LWIP}/core/ipv4/icmp.c
    ${LWIP}/core/ipv4/igmp.c
    ${LWIP}/core/ipv4/ip4.c
    ${LWIP}/core/ipv4/ip4_addr.c
    ${LWIP}/core/ipv4/ip4_frag.c
    # --- lwip ipv6 ---
    ${LWIP}/core/ipv6/ethip6.c
    ${LWIP}/core/ipv6/icmp6.c
    ${LWIP}/core/ipv6/ip6.c
    ${LWIP}/core/ipv6/ip6_addr.c
    ${LWIP}/core/ipv6/ip6_frag.c
    ${LWIP}/core/ipv6/mld6.c
    ${LWIP}/core/ipv6/nd6.c
    # --- netif ---
    ${LWIP}/netif/ethernet.c
    # --- lwip PPP ---
    ${LWIP}/netif/ppp/auth.c
    ${LWIP}/netif/ppp/ccp.c
    ${LWIP}/netif/ppp/chap-md5.c
    ${LWIP}/netif/ppp/chap_ms.c
    ${LWIP}/netif/ppp/chap-new.c
    ${LWIP}/netif/ppp/demand.c
    ${LWIP}/netif/ppp/eap.c
    ${LWIP}/netif/ppp/ecp.c
    ${LWIP}/netif/ppp/eui64.c
    ${LWIP}/netif/ppp/fsm.c
    ${LWIP}/netif/ppp/ipcp.c
    ${LWIP}/netif/ppp/ipv6cp.c
    ${LWIP}/netif/ppp/lcp.c
    ${LWIP}/netif/ppp/magic.c
    ${LWIP}/netif/ppp/mppe.c
    ${LWIP}/netif/ppp/multilink.c
    ${LWIP}/netif/ppp/ppp.c
    ${LWIP}/netif/ppp/pppapi.c
    ${LWIP}/netif/ppp/pppcrypt.c
    ${LWIP}/netif/ppp/pppoe.c
    ${LWIP}/netif/ppp/pppol2tp.c
    ${LWIP}/netif/ppp/pppos.c
    ${LWIP}/netif/ppp/upap.c
    ${LWIP}/netif/ppp/utils.c
    ${LWIP}/netif/ppp/vj.c
    # --- net_adapt ---
    ${SDK_ROOT}/platform/application/network/net_adapt/src/net_adapt.c
    ${SDK_ROOT}/platform/application/network/net_adapt/src/ps_netif_api.c
    ${SDK_ROOT}/platform/application/network/net_adapt/src/xy_wan_api.c
    ${SDK_ROOT}/platform/application/network/net_adapt/src/xy_tcpip_api.c
    ${SDK_ROOT}/platform/application/network/net_adapt/src/xy_socket_event.c
    # --- netpkt ---
    ${SDK_ROOT}/platform/application/netdata/netpkt/src/net_debug.c
    ${SDK_ROOT}/platform/application/netdata/netpkt/src/net_mem.c
    ${SDK_ROOT}/platform/application/netdata/netpkt/src/pkt_process.c
    ${SDK_ROOT}/platform/application/netdata/netpkt/src/fast_process.c
    # --- radvd / dhcpv6 ---
    ${SDK_ROOT}/platform/application/netdata/radvd/radvd_reply.c
    ${SDK_ROOT}/platform/application/netdata/dhcpv6/dhcp6_reply.c
    # --- PPP server ---
    ${SDK_ROOT}/platform/application/ppp/src/ppp_server.c
    # --- Phase 1 data-plane features ---
    ${SDK_ROOT}/platform/application/ping/xy_ping/src/at_ping.c
    ${SDK_ROOT}/platform/application/ping/xy_ping/src/ping_api.c
    ${SDK_ROOT}/platform/application/socket/socketMgr/src/socketMgr.c
    ${SDK_ROOT}/platform/application/socket/socketMgr/src/socketMgrApi.c
    ${SDK_ROOT}/platform/application/socket/socketMgr/src/at_socket_utils.c
    ${SDK_ROOT}/platform/application/socket/socket_app/src/socket_sync_api.c
    ${SDK_ROOT}/platform/application/socket/socket_cat1/default/src/at_socket_default.c
    ${SDK_ROOT}/platform/application/socket/socket_cat1/default/src/socket_default.c
    ${SDK_ROOT}/platform/application/socket/socket_cat1/default/src/socket_passthr.c
    ${SDK_ROOT}/platform/application/common/src/xy_walltime.c
    ${LWIP}/apps/sntp/sntp_client.c
    # --- common ---
    ${SDK_ROOT}/platform/application/common/src/app_utils.c
    ${SDK_ROOT}/platform/application/common/src/main_proxy.c
    # --- Network-side stubs (common to the SDK family) ---
    ${SIM_SDK_COMMON}/src/sim_net_shims.c
    ${SIM_SDK_COMMON}/src/sim_icc_shim.c
)

# --- Phase 2 conditional source files ---
if(ENABLE_AT_FS_QUEC)
    target_sources(xynet PRIVATE
        ${SDK_ROOT}/platform/application/fs/littlefs/lfs.c
        ${SDK_ROOT}/platform/application/fs/littlefs/lfs_util.c
        ${SDK_ROOT}/platform/application/fs/littlefs/fs_al.c
        ${SDK_ROOT}/platform/application/fs/xy_fs/src/fs_ext_cfg.c
        ${SDK_ROOT}/platform/application/fs/xy_fs/src/fs_proxy.c
        ${SDK_ROOT}/platform/application/fs/xy_fs/src/fs_util.c
        ${SDK_ROOT}/platform/application/fs/xy_fs/src/xy_fs_api.c
        ${SDK_ROOT}/platform/application/fs/xy_fs/src/xy_fs_error.c
        ${SDK_ROOT}/platform/application/fs/at_fs/src/at_fs.c
    )
endif()

if(ENABLE_AT_SSL_QUEC)
    target_sources(xynet PRIVATE
        ${SDK_ROOT}/platform/application/ssl/at_ssl/at_ssl_api.c
        ${SDK_ROOT}/platform/application/ssl/at_ssl/at_ssl_config.c
        ${SDK_ROOT}/platform/application/ssl/ssl_api/xy_ssl_api.c
        ${SDK_ROOT}/platform/application/ssl/xy_port/src/entropy_hardware_poll.c
        ${SDK_ROOT}/platform/application/ssl/xy_port/src/mbedtls_init.c
        ${SDK_ROOT}/platform/application/ssl/xy_port/src/net_sockets_alt.c
        ${SDK_ROOT}/platform/application/ssl/xy_port/src/timing_alt.c
        ${SDK_ROOT}/platform/application/socket/socket_cat1/default/src/at_socket_ssl.c
        ${SDK_ROOT}/platform/application/socket/socket_cat1/default/src/socket_ssl.c
    )
endif()

if(ENABLE_AT_HTTP_QUEC)
    target_sources(xynet PRIVATE
        ${SDK_ROOT}/platform/application/http/at_http/src/at_http.c
        ${SDK_ROOT}/platform/application/http/at_http/src/at_http_api.c
        ${SDK_ROOT}/platform/application/http/at_http/src/at_http_context.c
        ${SDK_ROOT}/platform/application/http/at_http/src/at_http_error.c
        ${SDK_ROOT}/platform/application/http/at_http/src/at_http_passthr.c
        ${SDK_ROOT}/platform/application/http/httpcore/src/http_client.c
        ${SDK_ROOT}/platform/application/http/httpcore/src/http_formdata.c
        ${SDK_ROOT}/platform/application/http/httpcore/src/http_method_api.c
        ${SDK_ROOT}/platform/application/http/httpcore/src/http_wrapper.c
        ${SDK_ROOT}/platform/application/http/xy_http/xy_http_adapt.c
    )
endif()

if(ENABLE_AT_FTP_QUEC)
    target_sources(xynet PRIVATE
        ${SDK_ROOT}/platform/application/ftp/at_ftp/default/src/at_ftp_default.c
        ${SDK_ROOT}/platform/application/ftp/at_ftp/default/src/ftp_default.c
        ${SDK_ROOT}/platform/application/ftp/at_ftp/default/src/ftp_default_ssl.c
        ${SDK_ROOT}/platform/application/ftp/at_ftp/default/src/ftp_passthr.c
        ${SDK_ROOT}/platform/application/ftp/xy_ftp/src/ftp_protocol.c
        ${SDK_ROOT}/platform/application/ftp/xy_ftp/src/ftp_utilis.c
        ${SDK_ROOT}/platform/application/ftp/xy_ftp/src/xy_ftp_client_api.c
        ${SDK_ROOT}/platform/application/ftp/xy_ftp/src/xy_ftp_client_demo.c
    )
endif()

if(ENABLE_AT_MQTT_QUEC)
    target_sources(xynet PRIVATE
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/src/at_mqtt_api.c
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/src/at_mqtt_context.c
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/src/at_mqtt_default.c
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/src/at_mqtt_list.c
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/src/at_mqtt_passthr.c
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/src/at_mqtt_urc.c
        ${SDK_ROOT}/platform/application/mqtt/mqtt_api/src/xy_mqtt_api.c
        ${SDK_ROOT}/platform/application/mqtt/mqtt_api/src/xy_mqtt_client.c
        ${SDK_ROOT}/platform/application/mqtt/mqtt_api/src/xy_mqtt_linklist.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTClient-C/src/MQTTClient.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTConnectClient.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTConnectServer.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTDeserializePublish.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTFormat.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTPacket.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTSerializePublish.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTSubscribeClient.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTSubscribeServer.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTUnsubscribeClient.c
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src/MQTTUnsubscribeServer.c
        ${SDK_ROOT}/platform/application/mqtt/xy_adapt/MQTTFreeRTOS.c
        ${SDK_ROOT}/platform/application/mqtt/xy_adapt/MQTTTLS.c
    )
endif()

target_include_directories(xynet PUBLIC
    # shadow: this layer's root directory (lwipopts.h shadows the SDK lwip_config version) + host shims
    ${SIM_SDK_COMMON}
    ${SIM_TARGET_DIR}/include_host
    ${SIM_SDK_COMMON}/include_host
    ${CMAKE_SOURCE_DIR}/sim-core/include
    # SDK directories
    ${SDK_ROOT}/platform/application/network/lwip/route/inc
    ${SDK_ROOT}/platform/application/network/lwip/nat/inc
    ${SDK_ROOT}/platform/application/netdata/netpkt/inc
    ${SDK_ROOT}/platform/application/netdata/radvd
    ${SDK_ROOT}/platform/application/netdata/dhcpv6
    ${SDK_ROOT}/platform/application/ppp/inc
    ${SDK_ROOT}/platform/application/common/inc
    ${SDK_ROOT}/platform/kernel/rtc/inc
    ${SDK_ROOT}/platform/kernel/usbx/ux_app/inc
    ${SDK_ROOT}/platform/application/ping/xy_ping/inc
    ${SDK_ROOT}/platform/application/socket/socketMgr/inc
    ${SDK_ROOT}/platform/application/socket/socket_app/inc
    ${SDK_ROOT}/platform/application/socket/socket_cat1/default/inc
)

if(ENABLE_AT_FS_QUEC)
    target_include_directories(xynet PUBLIC
        ${SDK_ROOT}/platform/application/fs/xy_fs/inc
        ${SDK_ROOT}/platform/application/fs/littlefs
        ${SDK_ROOT}/platform/application/fs/at_fs/inc
        ${SDK_ROOT}/platform/application/fs/at_fs_cmcc/inc
        ${SDK_ROOT}/boot/drivers/flash_adapt/inc
    )
endif()

if(ENABLE_AT_SSL_QUEC)
    target_include_directories(xynet PUBLIC
        ${SDK_ROOT}/platform/application/ssl/at_ssl
        ${SDK_ROOT}/platform/application/ssl/ssl_api
    )
endif()

if(ENABLE_AT_HTTP_QUEC)
    target_include_directories(xynet PUBLIC
        ${SDK_ROOT}/platform/application/http/at_http/inc
        ${SDK_ROOT}/platform/application/http/httpcore/include
        ${SDK_ROOT}/platform/application/http/httpcore/internal
        ${SDK_ROOT}/platform/application/http/xy_http
    )
endif()

if(ENABLE_AT_FTP_QUEC)
    target_include_directories(xynet PUBLIC
        ${SDK_ROOT}/platform/application/ftp/at_ftp/default/inc
        ${SDK_ROOT}/platform/application/ftp/xy_ftp/inc
        ${SDK_ROOT}/platform/cmiot/cm_at_ftp/inc
    )
endif()

if(ENABLE_AT_MQTT_QUEC)
    target_include_directories(xynet PUBLIC
        ${SDK_ROOT}/platform/application/mqtt/at_mqtt/inc
        ${SDK_ROOT}/platform/application/mqtt/mqtt_api/inc
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTClient-C/src
        ${SDK_ROOT}/platform/application/mqtt/paho_mqtt/MQTTPacket/src
        ${SDK_ROOT}/platform/application/mqtt/xy_adapt
    )
endif()

target_compile_options(xynet PRIVATE -Wno-unused-parameter -fno-strict-aliasing -Wno-discarded-qualifiers)
# SDK code is written with gnu89 inline semantics (e.g. the bare `inline`
# definitions in fast_process.c emit no external symbol under C99
# semantics): Release -O2 hides this via inlining, while Debug -O0 link
# reports undefined reference. -fgnu89-inline makes bare inline provide an
# external definition, so both configurations are correct
target_compile_options(xynet PRIVATE -fgnu89-inline)
target_compile_options(xynet PRIVATE -include ${SIM_SDK_COMMON}/include/sim_missing_decls.h)
target_link_libraries(xynet PUBLIC xyat xyos xymbed ws2_32 iphlpapi)

# ---------------------------------------------------------------------------
# simcommon -- SDK-family common host stubs (the chip-independent part of the former xy4101shim)
# ---------------------------------------------------------------------------
add_library(simcommon STATIC
    # --- Common startup skeleton (CLI/channels/logging/kernel boot; target only fills in boot_task) ---
    ${SIM_SDK_COMMON}/src/sim_main.c
    # --- Virtual devices and host IO ---
    ${SIM_SDK_COMMON}/src/sim_tty_device.c
    ${SIM_SDK_COMMON}/src/sim_pcap.c
    # --- Network stubs (fake CP / data plane / ICC) ---
    ${SIM_SDK_COMMON}/src/sim_vps_net.c
    ${SIM_SDK_COMMON}/src/sim_rtc_shims.c
    # --- AT stubs ---
    ${SIM_SDK_COMMON}/src/sim_at_shims.c
    ${SIM_SDK_COMMON}/src/sim_at_cmd_regist.c
    # --- System stubs (FLASH disk image) ---
    ${SIM_SDK_COMMON}/src/sim_flash.c
    # --- lwIP OS porting layer ---
    ${SIM_SDK_COMMON}/src/sim_sys_arch.c
    # Note: sim_shims.c / sim_factory_nv.c / sim_xylog.c are compiled into xyos (kernel translation units must be available before this library),
    #       sim_net_shims.c / sim_icc_shim.c are compiled into xynet (strongly bound to the network stack library).
)

target_include_directories(simcommon PUBLIC
    # shadow directories: chip-specific first, family-wide last; this layer's root directory serves lwipopts.h shadowing
    ${SIM_TARGET_DIR}/include
    ${SIM_TARGET_DIR}/include_host
    ${SIM_SDK_COMMON}
    ${SIM_SDK_COMMON}/include
    ${SIM_SDK_COMMON}/include_host
    ${CMAKE_SOURCE_DIR}/sim-core/include
)

target_compile_options(simcommon PRIVATE
    -Wno-unused-parameter -fno-strict-aliasing
)
target_compile_options(simcommon PRIVATE
    -include ${SIM_SDK_COMMON}/include/sim_missing_decls.h
)

target_link_libraries(simcommon PUBLIC simcore xyos xynet)