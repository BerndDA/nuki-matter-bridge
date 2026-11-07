#pragma once

#include <esp_matter_core.h>

// RAII guard for the CHIP stack lock, for code running outside the CHIP task.
// Safe if the calling task already holds the lock.
class ChipLock {
public:
    ChipLock() : mStatus(esp_matter::lock::chip_stack_lock(portMAX_DELAY)) {}
    ~ChipLock()
    {
        if (mStatus == esp_matter::lock::SUCCESS) {
            esp_matter::lock::chip_stack_unlock();
        }
    }
    ChipLock(const ChipLock &) = delete;
    ChipLock &operator=(const ChipLock &) = delete;

private:
    esp_matter::lock::status_t mStatus;
};
