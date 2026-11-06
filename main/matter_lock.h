#pragma once

#include <esp_err.h>

#include "lock_types.h"

namespace matter_lock {

// Creates the Matter node with a Door Lock endpoint and starts the Matter stack.
esp_err_t start();

// Thread-safe: may be called from any task (it hops onto the CHIP task).
void publishStatus(const BridgeStatus &status);

void factoryReset();

} // namespace matter_lock
