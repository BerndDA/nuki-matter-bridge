cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(PROJECT_VER_NUMBER 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(nuki_matter)

# Settings required by the Matter SDK (mirrors esp-matter / arduino-esp32 examples)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# RISC-V targets set -Wno-format but keep flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)

# With BT_NIMBLE_SECURITY_ENABLE=n (as nuki_hub uses) BLE_STORE_MAX_BONDS is 0, so
# esp-nimble-cpp declares zero-length bond arrays that GCC 14 flags. Harmless: NimBLE
# is asked for at most 0 entries.
idf_component_get_property(nimble_cpp_lib esp-nimble-cpp COMPONENT_LIB)
target_compile_options(${nimble_cpp_lib} PRIVATE -Wno-error=array-bounds)
