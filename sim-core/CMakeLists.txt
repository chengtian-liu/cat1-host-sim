# sim-core -- generic simulation core (no chip SDK dependency)
#
# This directory builds the simcore static library, depending only on the
# Windows API (ws2_32 + iphlpapi). It contains no SDK headers such as
# xy_*.h, lwIP, or FreeRTOS.
# Operations that need chip-specific capabilities (memory allocation,
# downlink injection, RTOS delays) go through the callback table in
# sim_core_api.h, bound at startup by the target-xxx directory.

set(SIMCORE_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(simcore STATIC
    ${SIMCORE_DIR}/src/sim_core_api.c
    ${SIMCORE_DIR}/src/sim_hostio.c
    ${SIMCORE_DIR}/src/sim_log.c
    ${SIMCORE_DIR}/src/sim_proxy.c
    ${SIMCORE_DIR}/src/sim_proxy_icmp.c
    ${SIMCORE_DIR}/src/sim_proxy_tcp.c
    ${SIMCORE_DIR}/src/sim_proxy_udp.c
)

target_include_directories(simcore PUBLIC
    ${SIMCORE_DIR}/include
)

target_compile_definitions(simcore PRIVATE
    WIN32_LEAN_AND_MEAN
)

target_compile_options(simcore PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

target_link_libraries(simcore PUBLIC ws2_32 iphlpapi)