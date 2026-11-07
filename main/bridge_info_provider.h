#pragma once

// Basic Information of the bridge itself (manufacturer / model / serial in controllers).
// Without a factory partition CHIP reports TEST_VENDOR / TEST_PRODUCT / TEST_SN; this wraps
// the active provider and overrides just those strings. Everything else (vendor/product
// IDs, rotating ID, ...) is delegated unchanged.

#include <cstdio>
#include <cstring>

#include <esp_mac.h>
#include <platform/DeviceInstanceInfoProvider.h>

class BridgeInfoProvider : public chip::DeviceLayer::DeviceInstanceInfoProvider {
public:
    explicit BridgeInfoProvider(chip::DeviceLayer::DeviceInstanceInfoProvider *base) : mBase(base)
    {
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(mSerial, sizeof(mSerial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                 mac[5]);
    }

    CHIP_ERROR GetVendorName(char *buf, size_t size) override { return copy(buf, size, "DIY"); }
    CHIP_ERROR GetProductName(char *buf, size_t size) override { return copy(buf, size, "Nuki Matter Bridge"); }
    CHIP_ERROR GetSerialNumber(char *buf, size_t size) override { return copy(buf, size, mSerial); }
    CHIP_ERROR GetHardwareVersionString(char *buf, size_t size) override { return copy(buf, size, "ESP32"); }

    CHIP_ERROR GetVendorId(uint16_t &v) override { return mBase->GetVendorId(v); }
    CHIP_ERROR GetProductId(uint16_t &v) override { return mBase->GetProductId(v); }
    CHIP_ERROR GetPartNumber(char *buf, size_t size) override { return mBase->GetPartNumber(buf, size); }
    CHIP_ERROR GetProductURL(char *buf, size_t size) override { return mBase->GetProductURL(buf, size); }
    CHIP_ERROR GetProductLabel(char *buf, size_t size) override { return mBase->GetProductLabel(buf, size); }
    CHIP_ERROR GetManufacturingDate(uint16_t &y, uint8_t &m, uint8_t &d) override
    {
        return mBase->GetManufacturingDate(y, m, d);
    }
    CHIP_ERROR GetHardwareVersion(uint16_t &v) override { return mBase->GetHardwareVersion(v); }
    CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan &span) override
    {
        return mBase->GetRotatingDeviceIdUniqueId(span);
    }
    CHIP_ERROR GetProductFinish(chip::app::Clusters::BasicInformation::ProductFinishEnum *finish) override
    {
        return mBase->GetProductFinish(finish);
    }
    CHIP_ERROR GetProductPrimaryColor(chip::app::Clusters::BasicInformation::ColorEnum *color) override
    {
        return mBase->GetProductPrimaryColor(color);
    }
    CHIP_ERROR GetSoftwareVersionString(char *buf, size_t size) override
    {
        return mBase->GetSoftwareVersionString(buf, size);
    }

private:
    static CHIP_ERROR copy(char *buf, size_t size, const char *value)
    {
        if (strlen(value) >= size) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, value);
        return CHIP_NO_ERROR;
    }

    chip::DeviceLayer::DeviceInstanceInfoProvider *mBase;
    char mSerial[13] = {};
};
