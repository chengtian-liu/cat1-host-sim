cmake_minimum_required(VERSION 3.16)
project(xysim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output layout: out/ holds only xysim.exe as the single deliverable; the link
# map goes to build/ (still usable for crash symbol resolution). The Debug
# configuration (build.bat debug, for gdb single-stepping) is placed in
# out-dbg/ via -DXYSIM_OUTDIR, so it never overwrites the Release deliverable
# (see XYSIM_OUTDIR at the end of target-xxx/CMakeLists.txt).

# ===========================================================================
# Three-layer architecture (split on 2026-09-28):
#   sim-core/        -- generic simulation core (pure Win32, zero SDK deps) -> simcore
#   sim-sdk-common/  -- layer shared by the SDK family: real SDK libraries
#                       (xyos/xyat/xynet/xymbed, all referenced read-only)
#                       + family-common host stubs (simcommon)
#   target-xxx/      -- chip adaptation layer: bridges/hooks/chip shims -> xysim.exe
#   third_party/     -- FreeRTOS Windows port
#
# To add a new chip product: copy the target-xy4101 skeleton (sim-target.cmake
# + CMakeLists.txt + src/) and change SIM_TARGET; the SDK libraries and common
# stubs are reused with zero modifications.
# ===========================================================================

# ---------------------------------------------------------------------------
# SDK root directory (shared by all products of the same family)
# ---------------------------------------------------------------------------
set(SDK_ROOT ${CMAKE_SOURCE_DIR}/../LTEcat1_SDK_AP
    CACHE PATH "Path to LTEcat1_SDK_AP root directory (any SDK path may be specified)")

if(NOT EXISTS "${SDK_ROOT}/platform/kernel/os/FreeRTOS")
    message(FATAL_ERROR
        "SDK_ROOT (${SDK_ROOT}) is not a valid LTEcat1_SDK_AP directory.\n"
        "  Please specify the correct SDK path, for example:\n"
        "    cmake -S . -B build -DSDK_ROOT=D:/path/to/LTEcat1_SDK_AP\n"
        "    build.bat --sdk D:/path/to/LTEcat1_SDK_AP\n"
        "  The default path is LTEcat1_SDK_AP in the parent directory of the simulator project.")
endif()

# ---------------------------------------------------------------------------
# Target chip product (target directory name)
# ---------------------------------------------------------------------------
set(SIM_TARGET target-xy4101 CACHE STRING
    "Target chip product directory (e.g. target-xy4101)")
set_property(CACHE SIM_TARGET PROPERTY STRINGS target-xy4101)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${SIM_TARGET}/sim-target.cmake")
    message(FATAL_ERROR
        "No sim-target.cmake found under the SIM_TARGET (${SIM_TARGET}) directory.\n"
        "  Every target-xxx directory must provide sim-target.cmake (the chip-difference injection point);\n"
        "  see target-xy4101/sim-target.cmake for reference.")
endif()

set(SIM_TARGET_DIR ${CMAKE_SOURCE_DIR}/${SIM_TARGET})

# Chip-specific parameters (SIM_CHIP_MEMMAP etc.) must be injected before the common layer
include(${SIM_TARGET_DIR}/sim-target.cmake)

# ---------------------------------------------------------------------------
# Subprojects: order = dependency order (simcore <- SDK libs/simcommon <- target)
# ---------------------------------------------------------------------------
add_subdirectory(sim-core)
add_subdirectory(sim-sdk-common)
add_subdirectory(${SIM_TARGET})
