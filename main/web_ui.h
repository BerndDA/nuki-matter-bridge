#pragma once

#include <esp_err.h>

namespace web_ui {

// Starts the status / maintenance web server on port 80. Call after the Matter stack is up.
esp_err_t start();

} // namespace web_ui
