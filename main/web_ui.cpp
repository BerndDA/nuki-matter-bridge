// Small status / maintenance web page on port 80 (no login, LAN only).
//
//   GET  /                         page (main/web/index.html, gzipped at build time)
//   GET  /api/status               JSON status
//   POST /api/restart              reboot
//   POST /api/commissioning        open the Matter pairing window for 5 min
//   POST /api/nuki-unpair          forget the Nuki pairing
//   POST /api/matter-reset         Matter factory reset (removes all controllers + Wi-Fi)
//   POST /api/ota                  firmware upload (raw .bin body, header X-OTA-Password)
//
// POSTs must carry "X-Requested-With: nuki-web". Browsers can't add that header to a
// cross-site request without a CORS preflight (which we never answer), so another web
// page open in the user's browser can't trigger these actions.

#include "web_ui.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>

#include <atomic>

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "chip_lock.h"
#include "matter_lock.h"
#include "nuki_bridge.h"

static const char *TAG = "web";

// Gzipped at build time (main/CMakeLists.txt); every current browser accepts gzip.
extern const char index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const char index_html_gz_end[] asm("_binary_index_html_gz_end");

namespace web_ui {
namespace {

httpd_handle_t s_server = nullptr;

// ---- JSON helpers ------------------------------------------------------------

void appendEscaped(std::string &out, const char *s, size_t len)
{
    out += '"';
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    out += '"';
}

void appendEscaped(std::string &out, const char *s)
{
    appendEscaped(out, s, strlen(s));
}

void appendf(std::string &out, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

const char *lockStateName(BridgeLockState s)
{
    switch (s) {
    case BridgeLockState::Locked:       return "locked";
    case BridgeLockState::Unlocked:     return "unlocked";
    case BridgeLockState::Unlatched:    return "unlatched";
    case BridgeLockState::Locking:      return "locking";
    case BridgeLockState::Unlocking:    return "unlocking";
    case BridgeLockState::Jammed:       return "jammed";
    case BridgeLockState::Uncalibrated: return "uncalibrated";
    default:                            return "unknown";
    }
}

const char *doorStateName(BridgeDoorState s)
{
    switch (s) {
    case BridgeDoorState::Open:   return "open";
    case BridgeDoorState::Closed: return "closed";
    case BridgeDoorState::Error:  return "error";
    default:                      return "n/a";
    }
}

const char *resetReasonName(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_SW:       return "software restart";
    case ESP_RST_PANIC:    return "crash (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT:      return "reset pin";
    default:               return "other";
    }
}

const char *vendorName(uint16_t vid)
{
    switch (vid) {
    case 0x1349: return "Apple Home";
    case 0x1384: return "Apple Keychain";
    case 0x6006: return "Google";
    case 0x1217: return "Amazon Alexa";
    case 0x110A: return "Samsung SmartThings";
    case 0x134B: return "Home Assistant";
    case 0xFFF1: return "Test vendor";
    default:     return "";
    }
}

// ---- status --------------------------------------------------------------------

void appendMatter(std::string &out)
{
    ChipLock lock;
    auto &server = chip::Server::GetInstance();

    out += "\"matter\":{";
    appendf(out, "\"commissioningOpen\":%s,",
            server.GetCommissioningWindowManager().IsCommissioningWindowOpen() ? "true" : "false");

    auto flags = chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork);
    char qr[64] = {};
    chip::MutableCharSpan qrSpan(qr);
    if (GetQRCode(qrSpan, flags) == CHIP_NO_ERROR) {
        out += "\"qr\":";
        appendEscaped(out, qrSpan.data(), qrSpan.size());
        out += ',';
    }
    char manual[32] = {};
    chip::MutableCharSpan manualSpan(manual);
    if (GetManualPairingCode(manualSpan, flags) == CHIP_NO_ERROR) {
        out += "\"manual\":";
        appendEscaped(out, manualSpan.data(), manualSpan.size());
        out += ',';
    }

    out += "\"fabrics\":[";
    bool first = true;
    for (const auto &fabric : server.GetFabricTable()) {
        if (!first) {
            out += ',';
        }
        first = false;
        uint16_t vid = chip::to_underlying(fabric.GetVendorId());
        uint64_t node = fabric.GetNodeId();
        appendf(out, "{\"index\":%u,\"vendorId\":%u,\"nodeId\":\"%08" PRIX32 "%08" PRIX32 "\",\"vendor\":",
                fabric.GetFabricIndex(), vid, static_cast<uint32_t>(node >> 32), static_cast<uint32_t>(node));
        appendEscaped(out, vendorName(vid));
        out += ",\"label\":";
        chip::CharSpan label = fabric.GetFabricLabel();
        appendEscaped(out, label.data(), label.size());
        out += '}';
    }
    out += "]}";
}

void appendWifi(std::string &out)
{
    out += "\"wifi\":{";
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out += "\"connected\":true,\"ssid\":";
        appendEscaped(out, reinterpret_cast<const char *>(ap.ssid));
        appendf(out, ",\"rssi\":%d,\"channel\":%u", ap.rssi, ap.primary);
    } else {
        out += "\"connected\":false";
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        appendf(out, ",\"ip\":\"" IPSTR "\"", IP2STR(&ip.ip));
    }
    uint8_t mac[6] = {};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        appendf(out, ",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\"", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    out += '}';
}

std::string buildStatus()
{
    std::string out;
    out.reserve(1024);

    const esp_app_desc_t *app = esp_app_get_description();
    out += "{\"system\":{\"version\":";
    appendEscaped(out, app->version);
    out += ",\"built\":";
    std::string built = std::string(app->date) + " " + app->time;
    appendEscaped(out, built.c_str());
    appendf(out, ",\"simulation\":%s", CONFIG_NUKI_SIMULATE ? "true" : "false");
    out += ",\"partition\":";
    appendEscaped(out, esp_ota_get_running_partition()->label);
    // newlib nano printf (CONFIG_NEWLIB_NANO_FORMAT) has no 64-bit formats: stay in 32 bit.
    appendf(out, ",\"uptimeS\":%" PRIu32, static_cast<uint32_t>(esp_timer_get_time() / 1000000));
    appendf(out, ",\"heapFree\":%" PRIu32 ",\"heapMin\":%" PRIu32, esp_get_free_heap_size(),
            esp_get_minimum_free_heap_size());
    out += ",\"resetReason\":";
    appendEscaped(out, resetReasonName(esp_reset_reason()));
    out += "},";

    BridgeStatus st;
    bool have = matter_lock::lastStatus(st);
    out += "\"lock\":{";
    appendf(out, "\"valid\":%s", have && st.valid ? "true" : "false");
    if (have && st.valid) {
        appendf(out, ",\"state\":\"%s\",\"door\":\"%s\",\"battery\":%u,\"batteryCritical\":%s", lockStateName(st.lock),
                doorStateName(st.door), st.batteryPercent, st.batteryCritical ? "true" : "false");
    }
    out += "},";

    appendf(out, "\"nuki\":{\"paired\":%s,\"name\":", nuki_bridge::isPaired() ? "true" : "false");
    appendEscaped(out, CONFIG_NUKI_DEVICE_NAME);
    out += "},";

    appendWifi(out);
    out += ',';
    appendMatter(out);
    out += '}';
    return out;
}

// ---- deferred actions --------------------------------------------------------

enum class Action { Restart, MatterReset };

void actionTask(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));  // let the HTTP response go out first
    switch (static_cast<Action>(reinterpret_cast<intptr_t>(arg))) {
    case Action::Restart:
        ESP_LOGW(TAG, "restart requested via web UI");
        esp_restart();
        break;
    case Action::MatterReset:
        ESP_LOGW(TAG, "Matter factory reset requested via web UI");
        matter_lock::factoryReset();  // reboots
        break;
    }
    vTaskDelete(nullptr);
}

void runDeferred(Action a)
{
    xTaskCreate(actionTask, "web_action", 4096, reinterpret_cast<void *>(static_cast<intptr_t>(a)), 5, nullptr);
}

// ---- handlers ----------------------------------------------------------------

esp_err_t sendJson(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

bool checkCsrf(httpd_req_t *req)
{
    char value[16] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Requested-With", value, sizeof(value)) == ESP_OK &&
        strcmp(value, "nuki-web") == 0) {
        return true;
    }
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "missing X-Requested-With header");
    return false;
}

esp_err_t handleIndex(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

esp_err_t handleStatus(httpd_req_t *req)
{
    std::string json = buildStatus();
    return sendJson(req, json.c_str());
}

esp_err_t handleRestart(httpd_req_t *req)
{
    if (!checkCsrf(req)) {
        return ESP_OK;
    }
    runDeferred(Action::Restart);
    return sendJson(req, "{\"ok\":true,\"message\":\"Restarting...\"}");
}

esp_err_t handleCommissioning(httpd_req_t *req)
{
    if (!checkCsrf(req)) {
        return ESP_OK;
    }
    bool ok = matter_lock::openCommissioningWindow() == ESP_OK;
    return sendJson(req, ok ? "{\"ok\":true,\"message\":\"Pairing window open for 5 minutes\"}"
                            : "{\"ok\":false,\"message\":\"Could not open the pairing window\"}");
}

esp_err_t handleNukiUnpair(httpd_req_t *req)
{
    if (!checkCsrf(req)) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Nuki unpair requested via web UI");
    nuki_bridge::unpair();
    return sendJson(req, "{\"ok\":true,\"message\":\"Nuki pairing removed. Hold the lock's button for 5 s to pair "
                         "again.\"}");
}

esp_err_t handleMatterReset(httpd_req_t *req)
{
    if (!checkCsrf(req)) {
        return ESP_OK;
    }
    runDeferred(Action::MatterReset);
    return sendJson(req, "{\"ok\":true,\"message\":\"Matter reset, restarting. Set Wi-Fi again via the serial "
                         "console.\"}");
}

// ---- OTA upload ----------------------------------------------------------------

std::atomic<bool> s_otaBusy{false};

bool passwordMatches(const char *given)
{
    const char *expected = CONFIG_NUKI_OTA_PASSWORD;
    size_t n = strlen(expected), m = strlen(given);
    unsigned diff = n ^ m;
    for (size_t i = 0; i < n; ++i) {  // constant time over the expected length
        diff |= (unsigned char)expected[i] ^ (unsigned char)(i < m ? given[i] : 0);
    }
    return diff == 0;
}

esp_err_t otaFail(httpd_req_t *req, const char *status, const char *message)
{
    ESP_LOGE(TAG, "OTA: %s", message);
    httpd_resp_set_status(req, status);
    std::string json = "{\"ok\":false,\"message\":";
    appendEscaped(json, message);
    json += '}';
    return sendJson(req, json.c_str());
}

// esp_err_to_name() only returns numbers here (CONFIG_ESP_ERR_TO_NAME_LOOKUP=n saves flash).
std::string otaError(const char *step, esp_err_t err)
{
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        return "not a valid firmware image for this chip";
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s failed (0x%x)", step, err);
    return buf;
}

esp_err_t receiveImage(httpd_req_t *req, const esp_partition_t *target, std::string &error)
{
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) {
        error = otaError("esp_ota_begin", err);
        return err;
    }

    constexpr size_t kChunk = 4096;
    std::unique_ptr<char[]> buf(new char[kChunk]);
    size_t remaining = req->content_len;
    int timeouts = 0;
    int lastPct = -1;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf.get(), remaining < kChunk ? remaining : kChunk);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) {
            continue;
        }
        if (n <= 0) {
            esp_ota_abort(ota);
            error = "upload interrupted";
            return ESP_FAIL;
        }
        timeouts = 0;
        if ((err = esp_ota_write(ota, buf.get(), n)) != ESP_OK) {
            esp_ota_abort(ota);
            error = otaError("esp_ota_write", err);
            return err;
        }
        remaining -= n;
        int pct = 100 - (int)(remaining * 100 / req->content_len);
        if (pct / 10 != lastPct / 10) {
            ESP_LOGI(TAG, "OTA: %d%%", pct);
            lastPct = pct;
        }
    }

    // Checks the image header, chip type and SHA-256.
    if ((err = esp_ota_end(ota)) != ESP_OK) {
        error = otaError("esp_ota_end", err);
        return err;
    }

    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(target, &desc) != ESP_OK ||
        strncmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)) != 0) {
        error = "image is not a nuki_matter firmware";
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA: received %s %s", desc.project_name, desc.version);
    return ESP_OK;
}

esp_err_t handleOta(httpd_req_t *req)
{
    char given[65] = {};
    if (strlen(CONFIG_NUKI_OTA_PASSWORD) == 0) {
        return otaFail(req, "403 Forbidden", "OTA is disabled (CONFIG_NUKI_OTA_PASSWORD is empty)");
    }
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Password", given, sizeof(given)) != ESP_OK ||
        !passwordMatches(given)) {
        return otaFail(req, "403 Forbidden", "wrong or missing X-OTA-Password");
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        return otaFail(req, "500 Internal Server Error", "no OTA partition");
    }
    if (req->content_len == 0 || req->content_len > target->size) {
        return otaFail(req, "413 Payload Too Large", "image size does not fit the OTA partition");
    }
    if (s_otaBusy.exchange(true)) {
        return otaFail(req, "409 Conflict", "another update is running");
    }

    ESP_LOGW(TAG, "OTA: receiving %u bytes into %s", (unsigned)req->content_len, target->label);
    std::string error;
    esp_err_t err = receiveImage(req, target, error);
    if (err == ESP_OK && (err = esp_ota_set_boot_partition(target)) != ESP_OK) {
        error = otaError("esp_ota_set_boot_partition", err);
    }
    s_otaBusy = false;
    if (err != ESP_OK) {
        return otaFail(req, "500 Internal Server Error", error.c_str());
    }

    runDeferred(Action::Restart);
    std::string json = "{\"ok\":true,\"message\":\"Update stored in ";
    json += target->label;
    json += ", restarting\"}";
    return sendJson(req, json.c_str());
}

} // namespace

esp_err_t start()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;  // status building takes the CHIP lock and formats ~1 KB of JSON
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, handleIndex, nullptr},
        {"/api/status", HTTP_GET, handleStatus, nullptr},
        {"/api/restart", HTTP_POST, handleRestart, nullptr},
        {"/api/commissioning", HTTP_POST, handleCommissioning, nullptr},
        {"/api/nuki-unpair", HTTP_POST, handleNukiUnpair, nullptr},
        {"/api/matter-reset", HTTP_POST, handleMatterReset, nullptr},
        {"/api/ota", HTTP_POST, handleOta, nullptr},
    };
    for (const auto &r : routes) {
        httpd_register_uri_handler(s_server, &r);
    }
    ESP_LOGI(TAG, "web UI on port %d", config.server_port);
    return ESP_OK;
}

} // namespace web_ui
