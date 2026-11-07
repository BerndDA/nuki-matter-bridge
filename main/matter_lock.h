#pragma once

#include <esp_err.h>

#include "lock_types.h"

namespace matter_lock {

// Creates the Matter node with a Door Lock endpoint and starts the Matter stack.
esp_err_t start();

// Thread-safe: may be called from any task (it hops onto the CHIP task).
void publishStatus(const BridgeStatus &status);

// Lock facts (serial, firmware) from the Nuki side; updates the bridged devices' Basic
// Information and the NVS cache. Thread-safe.
void publishInfo(const BridgeDeviceInfo &info);
bool lastInfo(BridgeDeviceInfo &out);

// Last status reported by the Nuki side. Thread-safe. Returns false before the first report.
bool lastStatus(BridgeStatus &out);

// Opens the commissioning window (DNS-SD, 5 min) if it isn't open. Thread-safe.
esp_err_t openCommissioningWindow();

// LockState attribute of the lock / "Open" endpoints as Matter reports it:
// 0 not fully locked, 1 locked, 2 unlocked, 3 unlatched, -1 null/absent. Takes the CHIP lock.
int reportedLockState(bool openEndpoint);

void factoryReset();

} // namespace matter_lock
