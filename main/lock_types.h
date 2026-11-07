#pragma once

#include <cstdint>

// Bridge-neutral representation of the lock, shared by the Nuki and Matter sides.
// Keeping this free of Nuki/CHIP headers keeps the two stacks decoupled.

enum class BridgeLockState : uint8_t {
    Unknown,
    Locked,
    Unlocked,
    Unlatched,
    Locking,     // motor running
    Unlocking,   // motor running (incl. unlatching)
    Jammed,      // motor blocked
    Uncalibrated,
};

enum class BridgeDoorState : uint8_t {
    NotAvailable,  // no door sensor
    Open,
    Closed,
    Error,
};

enum class BridgeLockCommand : uint8_t {
    Lock,
    Unlock,   // retract bolt, do not open
    Unlatch,  // retract bolt and pull the latch (opens the door)
};

// Static facts about the lock, read once after pairing (shown as manufacturer info).
struct BridgeDeviceInfo {
    char serial[16] = "";    // Nuki ID, hex as in the Nuki app
    char name[33] = "";      // name set in the Nuki app
    char firmware[16] = "";  // e.g. "4.2.6"
    char hardware[8] = "";   // e.g. "8.2"
    bool valid = false;
};

struct BridgeStatus {
    BridgeLockState lock = BridgeLockState::Unknown;
    BridgeDoorState door = BridgeDoorState::NotAvailable;
    uint8_t batteryPercent = 0;  // 0..100
    bool batteryCritical = false;
    bool valid = false;          // false until the first successful state read
};
