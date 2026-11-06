// arduino-esp32 (3.3.x, as an IDF component) adds linker --wrap flags for a few
// esp_matter / CHIP symbols whenever esp_matter is part of the build. The matching
// __wrap_ implementations live in Arduino's own Matter library, which we don't
// compile (CONFIG_ARDUINO_SELECTIVE_Matter=n: we use esp_matter directly).
// These pass-throughs restore the stock esp_matter / CHIP behaviour.

#include <cstddef>

#include <sdkconfig.h>

#if CONFIG_ESP_MATTER_ENABLE_DATA_MODEL

#include <lib/core/CHIPError.h>

extern "C" {

void *__real__Z21esp_matter_mem_callocjj(size_t n, size_t size);
void *__real__Z22esp_matter_mem_reallocPvj(void *ptr, size_t size);
void __real__Z19esp_matter_mem_freePv(void *ptr);

void *__wrap__Z21esp_matter_mem_callocjj(size_t n, size_t size)
{
    return __real__Z21esp_matter_mem_callocjj(n, size);
}

void *__wrap__Z22esp_matter_mem_reallocPvj(void *ptr, size_t size)
{
    return __real__Z22esp_matter_mem_reallocPvj(ptr, size);
}

void __wrap__Z19esp_matter_mem_freePv(void *ptr)
{
    __real__Z19esp_matter_mem_freePv(ptr);
}

#if CONFIG_ENABLE_WIFI_STATION
CHIP_ERROR __real__ZN4chip11DeviceLayer8Internal10ESP32Utils13InitWiFiStackEv(void);

CHIP_ERROR __wrap__ZN4chip11DeviceLayer8Internal10ESP32Utils13InitWiFiStackEv(void)
{
    return __real__ZN4chip11DeviceLayer8Internal10ESP32Utils13InitWiFiStackEv();
}
#endif

} // extern "C"

#endif // CONFIG_ESP_MATTER_ENABLE_DATA_MODEL
