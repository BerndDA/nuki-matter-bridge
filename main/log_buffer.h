#pragma once

#include <cstddef>
#include <cstdint>

// Keeps the most recent log output in a RAM ring buffer (from boot on) and streams it to
// TCP clients: `nc <bridge> 23` (or `make log`). Logs still go to the UART as well.
namespace log_buffer {

// Hooks esp_log. Call as early as possible in app_main.
void init();

// Starts the TCP log server (port 23, one client at a time). Call once the network stack is up.
void startServer();

// Copies log text starting at byte position `pos` (0 = oldest available) into `out`.
// Returns the number of bytes copied and advances `pos`. If `pos` was overwritten in the
// meantime it jumps to the oldest available byte and sets `dropped`.
size_t read(uint32_t &pos, char *out, size_t max, bool *dropped = nullptr);

// Position just after the newest byte.
uint32_t head();

} // namespace log_buffer
