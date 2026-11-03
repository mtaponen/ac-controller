#include "setup_portal.h"

#include "dns_server.h"

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include <nvs.h>
#include <driver/gpio.h>
#include <esp_system.h>

#include <cstring>

namespace esphome {
namespace setup_portal {

static const char *const TAG = "setup_portal";
static const uint32_t BUTTON_WATCH_MS = 2000;
static const uint32_t BUTTON_HOLD_MS = 200;
static const uint32_t PORTAL_KEY = 0x41504354;
static const uint32_t WIFI_STA_KEY = 88491487UL;
static const uint32_t ATTEMPT_MS = 10000;
static const uint8_t ATTEMPT_LIMIT = 3;
static const uint8_t PHASE_PORTAL = 0;
static const uint8_t PHASE_TRYING = 1;
static const uint8_t PHASE_OK = 2;

struct PortalState {
  uint8_t phase;
  char ssid[33];
  char wifi_password[65];
} PACKED;

static void append_escaped(std::string *out, const char *value) {
  if (value == nullptr) {
    return;
  }
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p != '\0'; p++) {
    switch (*p) {
      case '&':
        out->append("&amp;");
        break;
      case '"':
        out->append("&quot;");
        break;
      case '<':
        out->append("&lt;");
        break;
      case '>':
        out->append("&gt;");
        break;
      default:
        out->push_back(static_cast<char>(*p));
        break;
    }
  }
}

static const char *const SAVED_HTML =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>AC panel</title></head><body>"
    "<p>Saved. The panel is restarting and will join your Wi-Fi.</p>"
    "</body></html>";

static std::string error_page(const char *message) {
  std::string html =
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<title>AC panel</title></head><body><p>";
  html += message;
  html += "</p><p><a href=\"/\">Back</a></p></body></html>";
  return html;
}

SetupPortal::~SetupPortal() = default;

void SetupPortal::load_state_() {
  auto pref = global_preferences->make_preference<PortalState>(PORTAL_KEY, true);
  PortalState stored{};
  if (!pref.load(&stored)) {
    this->phase_ = PHASE_PORTAL;
    return;
  }
  stored.ssid[sizeof(stored.ssid) - 1] = '\0';
  stored.wifi_password[sizeof(stored.wifi_password) - 1] = '\0';
  this->phase_ = stored.phase;
  std::memcpy(this->ssid_, stored.ssid, sizeof(this->ssid_));
  std::memcpy(this->wifi_password_, stored.wifi_password, sizeof(this->wifi_password_));
}

void SetupPortal::save_state_() {
  PortalState stored{};
  stored.phase = this->phase_;
  std::memcpy(stored.ssid, this->ssid_, sizeof(stored.ssid));
  std::memcpy(stored.wifi_password, this->wifi_password_, sizeof(stored.wifi_password));
  stored.ssid[sizeof(stored.ssid) - 1] = '\0';
  stored.wifi_password[sizeof(stored.wifi_password) - 1] = '\0';
  auto pref = global_preferences->make_preference<PortalState>(PORTAL_KEY, true);
  pref.save(&stored);
  global_preferences->sync();
}

void SetupPortal::adopt_wifi_saved_() {
  if (this->ssid_[0] != '\0') {
    return;
  }
  auto pref = global_preferences->make_preference<wifi::SavedWifiSettings>(WIFI_STA_KEY, true);
  wifi::SavedWifiSettings saved{};
  if (!pref.load(&saved) || saved.ssid[0] == '\0') {
    return;
  }
  saved.ssid[sizeof(saved.ssid) - 1] = '\0';
  saved.password[sizeof(saved.password) - 1] = '\0';
  std::memcpy(this->ssid_, saved.ssid, sizeof(this->ssid_));
  std::memcpy(this->wifi_password_, saved.password, sizeof(this->wifi_password_));
  this->save_state_();
}

void SetupPortal::erase_wifi_sta_() {
  nvs_handle_t handle;
  if (nvs_open("esphome", NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGW(TAG, "Could not open storage to stop the saved Wi-Fi");
    return;
  }
  char key[UINT32_MAX_STR_SIZE];
  uint32_to_str(key, WIFI_STA_KEY);
  nvs_erase_key(handle, key);
  uint32_to_str(key, WIFI_STA_KEY + 1);
  nvs_erase_key(handle, key);
  nvs_commit(handle);
  nvs_close(handle);
}

void SetupPortal::mark_ok_() {
  if (this->phase_ == PHASE_OK) {
    return;
  }
  ESP_LOGI(TAG, "MelCloud reached the indoor unit. Later dropouts keep retrying.");
  this->phase_ = PHASE_OK;
  this->save_state_();
}

void SetupPortal::give_up_() {
  if (this->melcloud_ != nullptr && this->melcloud_->has_device()) {
    this->mark_ok_();
    return;
  }
  ESP_LOGW(TAG, "Three connection tries failed. Opening the setup form.");
  this->phase_ = PHASE_PORTAL;
  this->save_state_();
  this->erase_wifi_sta_();
  delay(100);
  App.safe_reboot();
}

void SetupPortal::watch_attempts_() {
  if (this->melcloud_ != nullptr && this->melcloud_->has_device()) {
    this->mark_ok_();
    return;
  }
  const uint32_t now = millis();
  if (this->window_start_ == 0) {
    this->window_start_ = now == 0 ? 1 : now;
    this->attempts_ = 1;
    ESP_LOGI(TAG, "Connection try 1 of %u", static_cast<unsigned>(ATTEMPT_LIMIT));
    return;
  }
  if (now - this->window_start_ < ATTEMPT_MS) {
    return;
  }
  if (this->attempts_ >= ATTEMPT_LIMIT) {
    this->give_up_();
    return;
  }
  this->attempts_++;
  this->window_start_ = now;
  ESP_LOGI(TAG, "Connection try %u of %u", static_cast<unsigned>(this->attempts_),
           static_cast<unsigned>(ATTEMPT_LIMIT));
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected() &&
      this->melcloud_ != nullptr) {
    this->melcloud_->kick_link();
  }
}

void SetupPortal::watch_button_() {
  if (esp_reset_reason() == ESP_RST_SW) {
    ESP_LOGI(TAG, "Software reboot, button watch skipped");
    return;
  }

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << GPIO_NUM_0;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&cfg) != ESP_OK) {
    ESP_LOGE(TAG, "GPIO0 could not be read. Saved info kept.");
    return;
  }

  ESP_LOGI(TAG, "Watching GPIO0 for 2s. Hold the button beside USB to clear saved info.");
  uint32_t low_since = 0;
  const uint32_t started = millis();
  bool held = false;
  while (millis() - started < BUTTON_WATCH_MS) {
    if (gpio_get_level(GPIO_NUM_0) == 0) {
      if (low_since == 0) {
        low_since = millis();
      }
      if (millis() - low_since >= BUTTON_HOLD_MS) {
        held = true;
        break;
      }
    } else {
      low_since = 0;
    }
    delay(10);
  }

  if (!held) {
    ESP_LOGI(TAG, "GPIO0 stayed high. Saved Wi-Fi and MelCloud info kept.");
    return;
  }

  ESP_LOGW(TAG, "GPIO0 held. Clearing saved Wi-Fi and MelCloud info.");
  global_preferences->reset();
  delay(100);
  App.safe_reboot();
}

void SetupPortal::setup() {
  this->watch_button_();
  this->load_state_();
  ESP_LOGI(TAG, "Boot phase %u, reset reason %d", static_cast<unsigned>(this->phase_),
           static_cast<int>(esp_reset_reason()));
  if (this->phase_ == PHASE_OK) {
    ESP_LOGI(TAG, "A connection has succeeded before. Keeping the saved network.");
    return;
  }
  // Save stores TRYING and reboots. That reboot is not always reported as a
  // software reset when the serial adapter is attached, so the phase alone
  // decides. A power cut during the tries starts them over.
  if (this->phase_ == PHASE_TRYING) {
    ESP_LOGI(TAG, "Trying the saved network up to %u times", static_cast<unsigned>(ATTEMPT_LIMIT));
    return;
  }
  if (this->phase_ != PHASE_PORTAL) {
    this->phase_ = PHASE_PORTAL;
    this->save_state_();
  }
  this->adopt_wifi_saved_();
  this->erase_wifi_sta_();
  ESP_LOGI(TAG, "No successful connection yet. Opening the setup hotspot.");
}

void SetupPortal::start_portal_() {
  this->base_->init();
  if (!this->handlers_added_) {
    this->base_->add_handler_without_auth(this);
    this->handlers_added_ = true;
  }
  this->dns_ = std::make_unique<DNSServer>();
  this->dns_->start(wifi::global_wifi_component->wifi_soft_ap_ip());
  this->active_ = true;
  ESP_LOGI(TAG, "Setup form is at http://192.168.4.1/");
}

void SetupPortal::loop() {
  if (this->phase_ == PHASE_TRYING) {
    this->watch_attempts_();
  }
  if (!this->active_) {
    if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_ap_active()) {
      this->start_portal_();
    }
    return;
  }
  if (this->dns_ != nullptr) {
    this->dns_->process_next_request();
  }
}

void SetupPortal::dump_config() {
  ESP_LOGCONFIG(TAG, "Setup portal:");
  ESP_LOGCONFIG(TAG, "  Hotspot: ac-controller");
  ESP_LOGCONFIG(TAG, "  Button: hold GPIO0 during the first 2s of a power-on");
}

float SetupPortal::get_setup_priority() const { return setup_priority::HARDWARE; }

bool SetupPortal::canHandle(AsyncWebServerRequest *request) const {
  (void) request;
  return this->active_;
}

std::string SetupPortal::form_html_() const {
  const char *email = this->melcloud_ != nullptr ? this->melcloud_->email().c_str() : "";
  const char *melcloud_password = this->melcloud_ != nullptr ? this->melcloud_->password().c_str() : "";
  const char *device_id = this->melcloud_ != nullptr ? this->melcloud_->wanted_device_id().c_str() : "";
  std::string html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AC panel</title>
<style>
body{font-family:sans-serif;margin:1.2rem;background:#101418;color:#f4f7fb;max-width:28rem}
label{display:block;margin-top:0.9rem;color:#93a0b0}
input[type=text],input[type=password]{width:100%;box-sizing:border-box;margin-top:0.3rem;padding:0.6rem;font-size:1rem}
.check{display:flex;align-items:center;gap:0.5rem;margin-top:1rem}
.check input{width:auto;margin:0}
button{margin-top:1.3rem;padding:0.7rem 1.2rem;font-size:1rem}
h2{font-size:1rem;margin:1.4rem 0 0}
p{color:#93a0b0}
</style>
</head>
<body>
<h1>AC panel</h1>
<form method="post" action="/save" accept-charset="UTF-8">
<h2>Wi-Fi</h2>
<label>Name
<input name="ssid" required maxlength="32" autocapitalize="none" autocorrect="off" spellcheck="false" value=")html";
  append_escaped(&html, this->ssid_);
  html += R"html("></label>
<label>Password
<input name="wifi_password" type="password" maxlength="64" autocomplete="off" value=")html";
  append_escaped(&html, this->wifi_password_);
  html += R"html("></label>
<h2>MelCloud</h2>
<label>Username
<input name="email" type="text" required maxlength="95" autocapitalize="none" autocorrect="off" spellcheck="false" inputmode="email" value=")html";
  append_escaped(&html, email);
  html += R"html("></label>
<label>Password
<input name="melcloud_password" type="password" required maxlength="63" autocomplete="off" value=")html";
  append_escaped(&html, melcloud_password);
  html += R"html("></label>
<label>Device id
<input name="device_id" maxlength="23" inputmode="numeric" autocapitalize="none" value=")html";
  append_escaped(&html, device_id);
  html += R"html("></label>
<p>Leave the device id empty if the account has one indoor unit.</p>
<label class="check"><input id="showpw" type="checkbox"> Show password</label>
<button type="submit">Save</button>
</form>
<script>
document.getElementById("showpw").addEventListener("change", function () {
  var kind = this.checked ? "text" : "password";
  document.querySelector("[name=wifi_password]").type = kind;
  document.querySelector("[name=melcloud_password]").type = kind;
});
</script>
</body>
</html>
)html";
  return html;
}

void SetupPortal::handle_save_(AsyncWebServerRequest *request) {
  const std::string ssid = request->arg("ssid");
  const std::string wifi_password = request->arg("wifi_password");
  const std::string email = request->arg("email");
  const std::string melcloud_password = request->arg("melcloud_password");
  const std::string device_id = request->arg("device_id");

  const char *error = nullptr;
  if (ssid.empty()) {
    error = "Wi-Fi name is required.";
  } else if (ssid.size() > 32) {
    error = "Wi-Fi name is too long.";
  } else if (wifi_password.size() > 64) {
    error = "Wi-Fi password is too long.";
  } else if (email.empty()) {
    error = "MelCloud username is required.";
  } else if (email.size() >= 96) {
    error = "MelCloud username is too long.";
  } else if (melcloud_password.empty()) {
    error = "MelCloud password is required.";
  } else if (melcloud_password.size() >= 64) {
    error = "MelCloud password is too long.";
  } else if (device_id.size() >= 24) {
    error = "Device id is too long.";
  }
  if (error != nullptr) {
    const std::string page = error_page(error);
    request->send(400, "text/html", page.c_str());
    return;
  }

  auto *response = request->beginResponse(200, "text/html", reinterpret_cast<const uint8_t *>(SAVED_HTML), strlen(SAVED_HTML));
  request->send(response);
  this->defer([this, ssid, wifi_password, email, melcloud_password, device_id]() {
    ESP_LOGI(TAG, "Saving Wi-Fi '%s' and MelCloud account", ssid.c_str());
    if (this->melcloud_ == nullptr || !this->melcloud_->save_credentials(email, melcloud_password, device_id)) {
      ESP_LOGE(TAG, "Could not store the MelCloud account");
      return;
    }
    std::memset(this->ssid_, 0, sizeof(this->ssid_));
    std::memset(this->wifi_password_, 0, sizeof(this->wifi_password_));
    std::memcpy(this->ssid_, ssid.data(), ssid.size());
    std::memcpy(this->wifi_password_, wifi_password.data(), wifi_password.size());
    this->phase_ = PHASE_TRYING;
    this->save_state_();
    wifi::global_wifi_component->save_wifi_sta(ssid.c_str(), wifi_password.c_str());
    delay(300);
    App.safe_reboot();
  });
}

void SetupPortal::handleRequest(AsyncWebServerRequest *request) {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(url_buf);
  if (request->method() == HTTP_POST && url == "/save") {
    this->handle_save_(request);
    return;
  }
  const std::string page = this->form_html_();
  request->send(200, "text/html", page.c_str());
}

}  // namespace setup_portal
}  // namespace esphome
