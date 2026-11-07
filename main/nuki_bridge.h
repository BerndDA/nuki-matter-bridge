#pragma once

#include "lock_types.h"

// Owns the BLE link to the Nuki lock. Runs in its own FreeRTOS task because every
// Nuki BLE command blocks for up to several seconds. Nothing in here may be called
// from the Matter (CHIP) task except enqueueCommand().
namespace nuki_bridge {

using StatusCallback = void (*)(const BridgeStatus &status);
using InfoCallback = void (*)(const BridgeDeviceInfo &info);

// Starts BLE + the Nuki task. Callbacks run on the Nuki task. onInfo is called once
// after (re)pairing / boot, when the lock's config could be read.
void start(StatusCallback onStatus, InfoCallback onInfo);

// Non-blocking; safe to call from any task.
bool enqueueCommand(BridgeLockCommand cmd);

bool isPaired();

// Pauses BLE scanning (e.g. during a firmware upload: Wi-Fi and BLE share the radio and the
// continuous scan slows Wi-Fi down a lot). Safe to call from any task.
void setScanningPaused(bool paused);

// Deletes stored Nuki credentials (the lock keeps the authorization entry until it
// is removed in the Nuki app).
void unpair();

} // namespace nuki_bridge
