# sim-target.cmake -- XY4101 chip-difference injection point
#
# sim-sdk-common/CMakeLists.txt (SDK libraries + family-common stubs) includes
# this file before add_subdirectory. All chip-product-specific build parameters
# converge here:
#
#   SIM_CHIP_MEMMAP  Name of the chip memory-layout directory under the SDK's
#                    memlayout/. Selects the memmap.c source file and the
#                    CONFIG_BOARD_MEMMAP macro (included internally by the
#                    simulator's memmap.h shim).
#
# For a new product, just copy this file (one line) + its own CMakeLists.txt
# (bridge/hooks).

set(SIM_CHIP_MEMMAP "XY4101PC")
