#include "log_buffer.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lib/support/logging/CHIPLogging.h>

static const char *TAG = "logbuf";

namespace log_buffer {
namespace {

constexpr size_t kSize = CONFIG_NUKI_LOG_BUFFER_KB * 1024;
constexpr int kPort = 23;
constexpr size_t kMaxLine = 256;  // formatted on the logging task's stack; longer lines are cut

char *s_buf = nullptr;
uint32_t s_head = 0;  // total bytes ever written; position in the ring = s_head % kSize
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_uartVprintf = nullptr;

void append(const char *data, size_t len)
{
    taskENTER_CRITICAL(&s_mux);
    for (size_t done = 0; done < len;) {
        size_t at = s_head % kSize;
        size_t n = len - done < kSize - at ? len - done : kSize - at;
        memcpy(s_buf + at, data + done, n);
        done += n;
        s_head += n;
    }
    taskEXIT_CRITICAL(&s_mux);
}

int logVprintf(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    char line[kMaxLine];
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        if ((size_t)n >= sizeof(line)) {  // cut: keep the line break so lines stay separate
            line[len - 1] = '\n';
        }
        append(line, len);
    }
    return s_uartVprintf ? s_uartVprintf(fmt, args) : n;
}

// CHIP's ESP32 log backend prints "I (ts) chip[MOD]: " and the line break with printf, past
// esp_log, so only the bare message would reach logVprintf. Format whole lines here instead.
void chipLogRedirect(const char *module, uint8_t category, const char *msg, va_list args)
{
    esp_log_level_t level;
    char letter;
    switch (category) {
    case chip::Logging::kLogCategory_Error:    level = ESP_LOG_ERROR;   letter = 'E'; break;
    case chip::Logging::kLogCategory_Progress: level = ESP_LOG_INFO;    letter = 'I'; break;
    case chip::Logging::kLogCategory_Detail:   level = ESP_LOG_DEBUG;   letter = 'D'; break;
    default:                                   level = ESP_LOG_VERBOSE; letter = 'V'; break;
    }
    char tag[16];
    snprintf(tag, sizeof(tag), "chip[%s]", module);
    if (level > esp_log_level_get(tag)) {
        return;
    }
    char line[kMaxLine];
    int n = snprintf(line, sizeof(line), "%c (%" PRIu32 ") %s: ", letter, esp_log_timestamp(), tag);
    va_list copy;
    va_copy(copy, args);
    if (n > 0 && (size_t)n < sizeof(line)) {
        n += vsnprintf(line + n, sizeof(line) - n, msg, copy);
    }
    va_end(copy);
    size_t len = n < 0 ? 0 : ((size_t)n < sizeof(line) - 1 ? (size_t)n : sizeof(line) - 2);
    line[len++] = '\n';
    line[len] = 0;
    append(line, len);
    fputs(line, stdout);  // UART console, as before
}

bool sendAll(int sock, const char *data, size_t len)
{
    while (len > 0) {
        int n = send(sock, data, len, 0);
        if (n <= 0) {
            return false;
        }
        data += n;
        len -= n;
    }
    return true;
}

void serveClient(int sock)
{
    // Short send timeout: a stalled client must not block the server for long.
    timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    static const char hello[] = "--- nuki_matter log (oldest buffered first); Ctrl+C to quit ---\n";
    if (!sendAll(sock, hello, sizeof(hello) - 1)) {
        return;
    }
    char chunk[512];
    uint32_t pos = 0;
    for (;;) {
        bool dropped = false;
        size_t n = read(pos, chunk, sizeof(chunk), &dropped);
        if (dropped) {
            static const char msg[] = "\n--- [log lines dropped: client too slow] ---\n";
            if (!sendAll(sock, msg, sizeof(msg) - 1)) {
                return;
            }
        }
        if (n > 0) {
            if (!sendAll(sock, chunk, n)) {
                return;
            }
            continue;
        }
        // Nothing new: wait a bit, and notice a closed connection.
        vTaskDelay(pdMS_TO_TICKS(200));
        char probe;
        int r = recv(sock, &probe, 1, MSG_DONTWAIT);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            return;
        }
    }
}

void serverTask(void *)
{
    int listener = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);  // dual stack: IPv4 + IPv6
    if (listener < 0) {
        ESP_LOGE(TAG, "socket failed: %d", errno);
        vTaskDelete(nullptr);
        return;
    }
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in6 addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(kPort);
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(listener, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen on port %d failed: %d", kPort, errno);
        close(listener);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "log stream on tcp port %d", kPort);

    for (;;) {
        int sock = accept(listener, nullptr, nullptr);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "log client connected");
        serveClient(sock);
        shutdown(sock, SHUT_RDWR);
        close(sock);
        ESP_LOGI(TAG, "log client disconnected");
    }
}

} // namespace

void init()
{
    s_buf = static_cast<char *>(heap_caps_malloc(kSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (!s_buf) {
        ESP_LOGE(TAG, "no memory for a %u KB log buffer", (unsigned)(kSize / 1024));
        return;
    }
    s_uartVprintf = esp_log_set_vprintf(logVprintf);
    chip::Logging::SetLogRedirectCallback(chipLogRedirect);
}

void startServer()
{
    if (s_buf) {
        xTaskCreate(serverTask, "log_srv", 3072, nullptr, 2, nullptr);
    }
}

size_t read(uint32_t &pos, char *out, size_t max, bool *dropped)
{
    if (!s_buf) {
        return 0;
    }
    taskENTER_CRITICAL(&s_mux);
    uint32_t oldest = s_head > kSize ? s_head - kSize : 0;
    if (pos < oldest || pos > s_head) {
        if (dropped && pos != 0) {
            *dropped = true;
        }
        pos = oldest;
    }
    size_t avail = s_head - pos;
    size_t n = avail < max ? avail : max;
    size_t at = pos % kSize;
    size_t first = n < kSize - at ? n : kSize - at;
    memcpy(out, s_buf + at, first);
    memcpy(out + first, s_buf, n - first);
    pos += n;
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

uint32_t head()
{
    taskENTER_CRITICAL(&s_mux);
    uint32_t h = s_head;
    taskEXIT_CRITICAL(&s_mux);
    return h;
}

} // namespace log_buffer
