# Wraps the Arduino-style Nuki BLE libraries (as pinned by nuki_hub) into an ESP-IDF component.
set(NUKI_HUB_LIB ${CMAKE_CURRENT_LIST_DIR}/../../third_party/nuki_hub/lib)

set(NUKI_DIR    ${NUKI_HUB_LIB}/nuki_ble/src)
set(SCANNER_DIR ${NUKI_HUB_LIB}/BleScanner/src)
set(CRC_DIR     ${NUKI_HUB_LIB}/CRC16/src)

if(NOT EXISTS ${NUKI_DIR}/NukiBle.cpp)
    message(FATAL_ERROR "third_party/nuki_hub is missing. Run: git submodule update --init --recursive")
endif()

file(GLOB NUKI_SRCS
    ${NUKI_DIR}/*.cpp
    ${SCANNER_DIR}/*.cpp
    ${CRC_DIR}/*.cpp)

idf_component_register(
    SRCS ${NUKI_SRCS}
    INCLUDE_DIRS ${NUKI_DIR} ${SCANNER_DIR} ${CRC_DIR}
    REQUIRES nvs_flash esp_timer bt)

# Same feature switches nuki_hub builds the library with. PUBLIC because several of
# them change struct layouts / APIs in headers that main/ includes.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    NUKI_ALT_CONNECT
    NUKI_USE_LATEST_NIMBLE
    BLESCANNER_USE_LATEST_NIMBLE
    NUKI_NO_WDT_RESET
    NUKI_MUTEX_RECURSIVE
    NUKI_64BIT_TIME)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-missing-field-initializers
    -Wno-maybe-uninitialized
    -Wno-implicit-fallthrough
    -Wno-unused-variable)
