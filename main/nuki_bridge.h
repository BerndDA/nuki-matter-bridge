#pragma once

#include "lock_types.h"

// Owns the BLE link to the Nuki lock. Runs in its own FreeRTOS task because every
// Nuki BLE command blocks for up to several seconds. Nothing in here may be called
// from the Matter (CHIP) task except enqueueCommand().
namespace nuki_bridge {

using StatusCallback = void (*)(const BridgeStatus &status);

// Starts BLE + the Nuki task. The callback runs on the Nuki task.
void start(StatusCallback onStatus);

// Non-blocking; safe to call from any task.
bool enqueueCommand(BridgeLockCommand cmd);

bool isPaired();

// Deletes stored Nuki credentials (the lock keeps the authorization entry until it
// is removed in the Nuki app).
void unpair();

} // namespace nuki_bridge
