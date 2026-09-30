# target-xy4101 -- XY4101 chip simulation adapter layer
#
# This directory contains only XY4101-chip-specific content:
#   include/       chip header shims such as csi_device.h (RISC-V CSI stub)
#   include_host/  XY4101 USB UDC header shims
#   src/           sim_xy4101_bridge.c (sim-core callbacks -> XY4101 SDK bindings)
#                  sim_xy4101_main.c (g_sim_target hook table + boot_task)
#
# Stubs common to the SDK family live in sim-sdk-common/ (simcommon library +
# xyos/xyat/xynet/xymbed SDK libraries); the pure Win32 common core lives in
# sim-core/ (simcore library).
# Chip-difference build parameters live in sim-target.cmake (already included
# by the top level).

set(TARGET_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(xy4101shim STATIC
    # --- Bridge layer (binds sim-core callbacks to the XY4101 SDK) ---
    ${TARGET_DIR}/src/sim_xy4101_bridge.c
)

target_include_directories(xy4101shim PUBLIC
    ${TARGET_DIR}/include
    ${TARGET_DIR}/include_host
)

target_compile_options(xy4101shim PRIVATE
    -Wno-unused-parameter -fno-strict-aliasing
)

target_link_libraries(xy4101shim PUBLIC simcommon simcore xyos xynet)

# ---- Executable ----
# Entry sim_xy4101_main.c: main() one-line tail-call to simcommon's generic
# skeleton sim_main(), and provides the g_sim_target hook table (boot_task /
# banner text).
add_executable(xysim
    ${TARGET_DIR}/src/sim_xy4101_main.c
)

target_link_libraries(xysim PRIVATE
    -Wl,--start-group xy4101shim simcommon xyat xynet xymbed -Wl,--end-group
)

target_compile_options(xysim PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_options(xysim PRIVATE -static -Wl,-Map=${CMAKE_BINARY_DIR}/xysim.map)

# Output directory: defaults to out/ (Release deliverables); build.bat debug
# passes -DXYSIM_OUTDIR=<src>/out-dbg so Debug builds are stored separately
# and do not overwrite the deliverables
set(XYSIM_OUTDIR ${CMAKE_SOURCE_DIR}/out CACHE PATH "xysim.exe output directory")
set_target_properties(xysim PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${XYSIM_OUTDIR})
