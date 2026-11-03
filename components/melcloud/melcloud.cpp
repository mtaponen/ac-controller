#include "melcloud.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include <ArduinoJson.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace esphome {
namespace melcloud {

static const char *const TAG = "melcloud";
static const uint32_t ACCOUNT_KEY = 0x4D454C31;

struct StoredAccount {
  char email[96];
  char password[64];
  char device_id[24];
};
static const char *const BASE = "https://app.melcloud.com/Mitsubishi.Wifi.Client";
static const size_t MAX_BODY = 196608;
static const uint32_t POLL_INTERVAL_MS = 5 * 60 * 1000;
static const uint32_t POLL_RETRY_MS = 15 * 1000;

static const int FLAG_POWER = 0x01;
static const int FLAG_MODE = 0x02;
static const int FLAG_TEMPERATURE = 0x04;
static const int FLAG_FAN = 0x08;
static const int FLAG_VANE_VERTICAL = 0x10;
static const int FLAG_VANE_HORIZONTAL = 0x100;

struct HttpResult {
  int status{0};
  std::string body;
  bool transport_ok{false};
};

static esp_err_t http_event(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 || event->user_data == nullptr) {
    return ESP_OK;
  }
  auto *out = static_cast<std::string *>(event->user_data);
  if (out->size() + static_cast<size_t>(event->data_len) > MAX_BODY) {
    return ESP_OK;
  }
  out->append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
  return ESP_OK;
}

static HttpResult http_call(const std::string &url, bool post, const std::string &body, const std::string &token) {
  HttpResult result;
  std::string response;
  response.reserve(2048);

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  config.timeout_ms = 20000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.event_handler = http_event;
  config.user_data = &response;
  config.buffer_size = 4096;
  config.buffer_size_tx = 2048;
  config.keep_alive_enable = false;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGW(TAG, "HTTP client init failed");
    return result;
  }

  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Accept-Encoding", "identity");
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "X-Requested-With", "XMLHttpRequest");
  esp_http_client_set_header(client, "Cookie", "policyaccepted=true");
  esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (compatible; ac-controller)");
  if (!token.empty()) {
    esp_http_client_set_header(client, "X-MitsContextKey", token.c_str());
  }
  if (post) {
    esp_http_client_set_post_field(client, body.data(), body.size());
  }

  const esp_err_t err = esp_http_client_perform(client);
  result.status = esp_http_client_get_status_code(client);
  result.body = std::move(response);
  result.transport_ok = err == ESP_OK;
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(err));
  } else if (result.status < 200 || result.status >= 300) {
    ESP_LOGW(TAG, "HTTP %d from MelCloud", result.status);
  }
  return result;
}

static bool http_ok(const HttpResult &result) {
  return result.transport_ok && result.status >= 200 && result.status < 300;
}

static float json_number(JsonVariant value, float fallback) {
  if (value.isNull() || value.is<bool>() || value.is<const char *>()) {
    return fallback;
  }
  return value.as<float>();
}

static int json_int(JsonVariant value, int fallback) {
  if (value.isNull()) {
    return fallback;
  }
  return value.as<int>();
}

void MelCloud::set_choice_(Choice &choice, int value, const char *label) {
  choice.value = value;
  std::snprintf(choice.label, sizeof(choice.label), "%s", label);
}

static float round_to_step(float value, float step) {
  if (step < 0.1f) {
    step = 0.5f;
  }
  return std::round(value / step) * step;
}

static std::string one_decimal(float value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

void MelCloud::load_credentials_() {
  auto pref = global_preferences->make_preference<StoredAccount>(ACCOUNT_KEY, true);
  StoredAccount stored{};
  if (!pref.load(&stored)) {
    ESP_LOGI(TAG, "No saved MelCloud account");
    return;
  }
  stored.email[sizeof(stored.email) - 1] = '\0';
  stored.password[sizeof(stored.password) - 1] = '\0';
  stored.device_id[sizeof(stored.device_id) - 1] = '\0';
  this->email_ = stored.email;
  this->password_ = stored.password;
  this->wanted_device_id_ = stored.device_id;
  if (this->email_.empty()) {
    ESP_LOGI(TAG, "No saved MelCloud account");
    return;
  }
  ESP_LOGI(TAG, "Loaded MelCloud account");
}

bool MelCloud::save_credentials(const std::string &email, const std::string &password,
                                const std::string &device_id) {
  if (email.size() >= sizeof(StoredAccount::email) || password.size() >= sizeof(StoredAccount::password) ||
      device_id.size() >= sizeof(StoredAccount::device_id)) {
    return false;
  }
  StoredAccount stored{};
  memcpy(stored.email, email.data(), email.size());
  memcpy(stored.password, password.data(), password.size());
  memcpy(stored.device_id, device_id.data(), device_id.size());
  auto pref = global_preferences->make_preference<StoredAccount>(ACCOUNT_KEY, true);
  if (!pref.save(&stored)) {
    return false;
  }
  global_preferences->sync();
  this->email_ = email;
  this->password_ = password;
  this->wanted_device_id_ = device_id;
  {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->device_ready_ = false;
  }
  this->token_.clear();
  return true;
}

bool MelCloud::has_device() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->device_ready_;
}

void MelCloud::kick_link() { this->next_attempt_ms_ = 0; }

void MelCloud::setup() {
  this->load_credentials_();
  xTaskCreatePinnedToCore(MelCloud::task_thunk_, "melcloud", 16384, this, 4, nullptr, 0);
}

void MelCloud::dump_config() {
  ESP_LOGCONFIG(TAG, "MELCloud:");
  ESP_LOGCONFIG(TAG, "  TLS certificate check: yes");
  ESP_LOGCONFIG(TAG, "  Account: %s", this->email_.empty() ? "not set" : "saved");
  if (!this->wanted_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Pinned device: %s", this->wanted_device_id_.c_str());
  }
}

float MelCloud::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void MelCloud::task_thunk_(void *param) { static_cast<MelCloud *>(param)->task_loop_(); }

void MelCloud::task_loop_() {
  for (;;) {
    if (!network::is_connected()) {
      std::lock_guard<std::mutex> lock(this->mu_);
      if (this->status_ == LinkStatus::LIVE) {
        this->status_ = LinkStatus::OFFLINE;
        this->shown_.online = false;
        this->revert_pending_();
      }
      // The 5-minute poll timer belongs to the last live reading. A
      // restored network should read MelCloud on the next loop.
      this->next_poll_ms_ = 0;
    } else if (this->email_.empty() || this->password_.empty()) {
      // The setup page stores an account before the first login.
    } else if (this->token_.empty()) {
      if (!this->login_blocked_ && millis() >= this->next_attempt_ms_) {
        this->login_();
      }
    } else if (this->device_id_.empty()) {
      if (millis() >= this->next_attempt_ms_) {
        this->list_devices_();
      }
    } else {
      bool write_due = false;
      {
        std::lock_guard<std::mutex> lock(this->mu_);
        write_due = this->any_pending_unlocked_() && millis() >= this->pending_.ready_at;
      }
      if (write_due && millis() >= this->next_write_ms_) {
        // Refresh first so a temperature write cannot send a stale fan
        // (or any other field) over a change made on another panel.
        if (this->poll_(false)) {
          this->write_one_();
        } else {
          this->next_write_ms_ = millis() + POLL_RETRY_MS;
        }
      } else if (!write_due && millis() >= this->next_poll_ms_) {
        this->poll_(true);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool MelCloud::any_pending_unlocked_() const {
  return this->pending_.power || this->pending_.temp || this->pending_.mode || this->pending_.fan ||
         this->pending_.vane_h || this->pending_.vane_v;
}

void MelCloud::revert_pending_() {
  this->pending_ = {};
  this->shown_.power = this->confirmed_.power;
  this->shown_.set_temp = this->confirmed_.set_temp;
  this->shown_.mode = this->confirmed_.mode;
  this->shown_.fan = this->confirmed_.fan;
  this->shown_.vane_h = this->confirmed_.vane_h;
  this->shown_.vane_v = this->confirmed_.vane_v;
}

bool MelCloud::login_() {
  JsonDocument doc;
  doc["Email"] = this->email_;
  doc["Password"] = this->password_;
  doc["Language"] = 0;
  doc["AppVersion"] = "1.19.1.1";
  doc["Persist"] = true;
  doc["CaptchaResponse"] = nullptr;
  std::string body;
  serializeJson(doc, body);

  const HttpResult result = http_call(std::string(BASE) + "/Login/ClientLogin", true, body, "");
  JsonDocument response;
  const bool parsed = http_ok(result) && deserializeJson(response, result.body) == DeserializationError::Ok;
  const char *key = parsed ? response["LoginData"]["ContextKey"].as<const char *>() : nullptr;
  if (key != nullptr && key[0] != '\0') {
    this->token_ = key;
    this->login_blocked_ = false;
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = LinkStatus::CONNECTING;
    ESP_LOGI(TAG, "Logged in");
    return true;
  }

  const char *message = parsed ? response["ErrorMessage"].as<const char *>() : nullptr;
  std::string error = message != nullptr ? message : "";
  std::string lowered = error;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool captcha = lowered.find("captcha") != std::string::npos;
  {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = captcha ? LinkStatus::CAPTCHA : LinkStatus::LOGIN_FAILED;
    this->shown_.online = false;
  }
  if (captcha) {
    this->login_blocked_ = true;
    ESP_LOGW(TAG, "MelCloud asked for a captcha. This panel cannot solve it.");
  } else if (!error.empty()) {
    ESP_LOGW(TAG, "Login failed: %s", error.c_str());
  } else {
    ESP_LOGW(TAG, "Login failed (HTTP %d)", result.status);
  }
  this->next_attempt_ms_ = millis() + 15000;
  return false;
}

static int device_type_of(JsonObject dev) {
  if (!dev["DeviceType"].isNull()) {
    return dev["DeviceType"].as<int>();
  }
  if (!dev["Device"]["DeviceType"].isNull()) {
    return dev["Device"]["DeviceType"].as<int>();
  }
  return -1;
}

static void consider_device(JsonObject dev, std::vector<JsonObject> *found) {
  if (dev.isNull() || device_type_of(dev) != 0) {
    return;
  }
  found->push_back(dev);
}

static void collect_devices(JsonVariant devices, std::vector<JsonObject> *found) {
  if (!devices.is<JsonArray>()) {
    return;
  }
  for (JsonObject dev : devices.as<JsonArray>()) {
    consider_device(dev, found);
  }
}

bool MelCloud::list_devices_() {
  const HttpResult result = http_call(std::string(BASE) + "/User/ListDevices", false, "", this->token_);
  if (result.status == 401) {
    this->token_.clear();
    return false;
  }
  JsonDocument doc;
  if (!http_ok(result) || deserializeJson(doc, result.body) != DeserializationError::Ok || !doc.is<JsonArray>()) {
    ESP_LOGW(TAG, "Device list unreadable (HTTP %d, %u bytes)", result.status,
             static_cast<unsigned>(result.body.size()));
    this->next_attempt_ms_ = millis() + 15000;
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = LinkStatus::OFFLINE;
    this->shown_.online = false;
    return false;
  }

  std::vector<JsonObject> found;
  for (JsonObject entry : doc.as<JsonArray>()) {
    JsonObject structure = entry["Structure"];
    collect_devices(structure["Devices"], &found);
    if (structure["Areas"].is<JsonArray>()) {
      for (JsonObject area : structure["Areas"].as<JsonArray>()) {
        collect_devices(area["Devices"], &found);
      }
    }
    if (structure["Floors"].is<JsonArray>()) {
      for (JsonObject floor : structure["Floors"].as<JsonArray>()) {
        collect_devices(floor["Devices"], &found);
        if (floor["Areas"].is<JsonArray>()) {
          for (JsonObject area : floor["Areas"].as<JsonArray>()) {
            collect_devices(area["Devices"], &found);
          }
        }
      }
    }
  }

  JsonObject chosen;
  for (JsonObject dev : found) {
    const char *id = dev["DeviceID"].as<const char *>();
    const std::string id_text = dev["DeviceID"].is<const char *>() ? id : std::to_string(dev["DeviceID"].as<long long>());
    const char *name = dev["DeviceName"].as<const char *>();
    ESP_LOGI(TAG, "Air-to-air device %s (%s)", id_text.c_str(), name != nullptr ? name : "");
    if (!this->wanted_device_id_.empty() && id_text == this->wanted_device_id_) {
      chosen = dev;
    }
  }
  if (chosen.isNull() && this->wanted_device_id_.empty() && found.size() == 1) {
    chosen = found[0];
  }
  if (chosen.isNull() && this->wanted_device_id_.empty() && found.size() > 1) {
    chosen = found[0];
    ESP_LOGW(TAG, "Account has %u indoor units. Using the first. Set melcloud_device_id to pin one.",
             static_cast<unsigned>(found.size()));
  }
  if (chosen.isNull()) {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = LinkStatus::NO_DEVICE;
    this->shown_.online = false;
    this->next_attempt_ms_ = millis() + 60000;
    ESP_LOGW(TAG, "No matching air-to-air device");
    return false;
  }

  this->device_id_ = chosen["DeviceID"].is<const char *>() ? chosen["DeviceID"].as<const char *>()
                                                           : std::to_string(chosen["DeviceID"].as<long long>());
  this->building_id_ = chosen["BuildingID"].is<const char *>()
                           ? chosen["BuildingID"].as<const char *>()
                           : std::to_string(chosen["BuildingID"].as<long long>());
  const char *name = chosen["DeviceName"].as<const char *>();
  this->device_name_ = name != nullptr ? name : "";

  JsonObject device = chosen["Device"];
  Limits limits = this->limits_;
  limits.step = json_number(device["TemperatureIncrement"], 0.5f);
  if (limits.step < 0.1f) {
    limits.step = 0.5f;
  }
  limits.min_heat = json_number(device["MinTempHeat"], limits.min_heat);
  limits.max_heat = json_number(device["MaxTempHeat"], limits.max_heat);
  limits.min_cool = json_number(device["MinTempCoolDry"], limits.min_cool);
  limits.max_cool = json_number(device["MaxTempCoolDry"], limits.max_cool);
  limits.min_auto = json_number(device["MinTempAutomatic"], limits.min_auto);
  limits.max_auto = json_number(device["MaxTempAutomatic"], limits.max_auto);

  const bool hide = device["HideVaneControls"].as<bool>();
  {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->limits_ = limits;
    this->auto_fan_ = device["HasAutomaticFanSpeed"].as<bool>();
    this->vane_h_enabled_ = !hide && device["ModelSupportsVaneHorizontal"].as<bool>();
    this->vane_v_enabled_ = !hide && device["ModelSupportsVaneVertical"].as<bool>();
    this->swing_ = device["SwingFunction"].as<bool>();
    this->caps_ready_ = true;
    this->device_ready_ = true;
    this->rebuild_options_();
  }
  ESP_LOGI(TAG, "Using %s (device %s)", this->device_name_.c_str(), this->device_id_.c_str());
  this->next_poll_ms_ = 0;
  return true;
}

bool MelCloud::poll_(bool revert_pending) {
  const std::string url = std::string(BASE) + "/Device/Get?id=" + this->device_id_ + "&buildingID=" + this->building_id_;
  const HttpResult result = http_call(url, false, "", this->token_);
  if (result.status == 401) {
    this->token_.clear();
    return false;
  }
  if (!http_ok(result)) {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = LinkStatus::OFFLINE;
    this->shown_.online = false;
    this->confirmed_.online = false;
    if (revert_pending) {
      this->revert_pending_();
    }
    this->next_poll_ms_ = millis() + POLL_RETRY_MS;
    return false;
  }
  this->ingest_state_(result.body, true);
  this->next_poll_ms_ = millis() + POLL_INTERVAL_MS;
  return true;
}

bool MelCloud::write_one_() {
  int flag = 0;
  Pending pending;
  {
    std::lock_guard<std::mutex> lock(this->mu_);
    pending = this->pending_;
  }
  if (pending.power && !pending.power_value) {
    flag = FLAG_POWER;
  } else if (pending.power && pending.mode) {
    flag = FLAG_POWER | FLAG_MODE;
  } else if (pending.power) {
    flag = FLAG_POWER;
  } else if (pending.temp) {
    flag = FLAG_TEMPERATURE;
  } else if (pending.mode) {
    flag = FLAG_MODE;
  } else if (pending.fan) {
    flag = FLAG_FAN;
  } else if (pending.vane_v) {
    flag = FLAG_VANE_VERTICAL;
  } else if (pending.vane_h) {
    flag = FLAG_VANE_HORIZONTAL;
  } else {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, this->last_state_) != DeserializationError::Ok || !doc.is<JsonObject>()) {
    ESP_LOGW(TAG, "Cannot write before a valid device state exists");
    std::lock_guard<std::mutex> lock(this->mu_);
    this->revert_pending_();
    return false;
  }
  JsonObject state = doc.as<JsonObject>();
  if ((flag & FLAG_POWER) != 0) {
    state["Power"] = pending.power_value;
  }
  if (flag == FLAG_TEMPERATURE) {
    state["SetTemperature"] = round_to_step(pending.temp_value, this->limits_.step);
  } else if ((flag & FLAG_MODE) != 0) {
    state["OperationMode"] = pending.mode_value;
  } else if (flag == FLAG_FAN) {
    state["SetFanSpeed"] = pending.fan_value;
  } else if (flag == FLAG_VANE_VERTICAL) {
    state["VaneVertical"] = pending.vane_v_value;
  } else if (flag == FLAG_VANE_HORIZONTAL) {
    state["VaneHorizontal"] = pending.vane_h_value;
  }
  state["EffectiveFlags"] = flag;
  state["HasPendingCommand"] = true;
  state["DeviceType"] = 0;
  std::string body;
  serializeJson(doc, body);

  const HttpResult result = http_call(std::string(BASE) + "/Device/SetAta", true, body, this->token_);
  if (result.status == 401) {
    this->token_.clear();
    return false;
  }
  if (!http_ok(result)) {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = LinkStatus::OFFLINE;
    this->shown_.online = false;
    this->revert_pending_();
    this->next_poll_ms_ = millis() + POLL_RETRY_MS;
    return false;
  }

  this->ingest_state_(result.body, false);
  bool more = false;
  uint32_t hold_poll_until = 0;
  {
    std::lock_guard<std::mutex> lock(this->mu_);
    if ((flag & FLAG_POWER) != 0 && this->pending_.power && this->pending_.power_value == pending.power_value) {
      this->pending_.power = false;
    }
    if (flag == FLAG_TEMPERATURE && this->pending_.temp &&
        std::fabs(this->pending_.temp_value - pending.temp_value) < 0.01f) {
      this->pending_.temp = false;
    } else if ((flag & FLAG_MODE) != 0 && this->pending_.mode && this->pending_.mode_value == pending.mode_value) {
      this->pending_.mode = false;
    } else if (flag == FLAG_FAN && this->pending_.fan && this->pending_.fan_value == pending.fan_value) {
      this->pending_.fan = false;
    } else if (flag == FLAG_VANE_VERTICAL && this->pending_.vane_v &&
               this->pending_.vane_v_value == pending.vane_v_value) {
      this->pending_.vane_v = false;
    } else if (flag == FLAG_VANE_HORIZONTAL && this->pending_.vane_h &&
               this->pending_.vane_h_value == pending.vane_h_value) {
      this->pending_.vane_h = false;
    }
    more = this->any_pending_unlocked_();
    if (more) {
      this->pending_.ready_at = millis() + 1000;
      hold_poll_until = this->pending_.ready_at;
    }
  }
  // Read the device back once the queued edits are sent. A further edit
  // waits its debounce first, so a poll cannot land between two writes.
  if (more) {
    if (this->next_poll_ms_ < hold_poll_until) {
      this->next_poll_ms_ = hold_poll_until;
    }
  } else {
    this->next_poll_ms_ = 0;
  }
  ESP_LOGI(TAG, "Wrote one field (flags 0x%X)", flag);
  return true;
}

void MelCloud::ingest_state_(const std::string &json, bool from_server) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || doc["SetTemperature"].isNull()) {
    ESP_LOGW(TAG, "State response was not a device document");
    std::lock_guard<std::mutex> lock(this->mu_);
    this->status_ = LinkStatus::OFFLINE;
    this->shown_.online = false;
    return;
  }
  this->last_state_ = json;
  Reading reading;
  reading.has_state = true;
  reading.online = true;
  reading.power = doc["Power"].as<bool>();
  reading.mode = json_int(doc["OperationMode"], 1);
  reading.set_temp = json_number(doc["SetTemperature"], 20);
  reading.room = json_number(doc["RoomTemperature"], 0);
  reading.fan = json_int(doc["SetFanSpeed"], 0);
  reading.vane_h = json_int(doc["VaneHorizontal"], 0);
  reading.vane_v = json_int(doc["VaneVertical"], 0);
  const int speeds = json_int(doc["NumberOfFanSpeeds"], -1);

  {
    std::lock_guard<std::mutex> lock(this->mu_);
    this->confirmed_ = reading;
    this->status_ = LinkStatus::LIVE;
    this->shown_.has_state = true;
    this->shown_.online = true;
    if (!this->pending_.power) {
      this->shown_.power = reading.power;
    }
    this->shown_.room = reading.room;
    if (!this->pending_.temp) {
      this->shown_.set_temp = reading.set_temp;
    }
    if (!this->pending_.mode) {
      this->shown_.mode = reading.mode;
    }
    if (!this->pending_.fan) {
      this->shown_.fan = reading.fan;
    }
    if (!this->pending_.vane_h) {
      this->shown_.vane_h = reading.vane_h;
    }
    if (!this->pending_.vane_v) {
      this->shown_.vane_v = reading.vane_v;
    }
    if (speeds >= 0) {
      this->fan_speeds_ = speeds;
    }
    this->rebuild_options_();
  }
  if (from_server) {
    ESP_LOGI(TAG, "%s  set %s  room %s  power %s", mode_word_(reading.mode), one_decimal(reading.set_temp).c_str(),
             one_decimal(reading.room).c_str(), reading.power ? "on" : "off");
  }
}

void MelCloud::rebuild_options_() {
  this->fan_count_ = 0;
  if (this->auto_fan_ && this->fan_count_ < 6) {
    set_choice_(this->fan_[this->fan_count_++], 0, "Auto");
  }
  const int speeds = std::max(0, std::min(this->fan_speeds_, 5));
  for (int speed = 1; speed <= speeds && this->fan_count_ < 6; speed++) {
    char label[8];
    std::snprintf(label, sizeof(label), "%d", speed);
    set_choice_(this->fan_[this->fan_count_++], speed, label);
  }

  this->vane_h_count_ = 0;
  if (this->vane_h_enabled_) {
    const int values[] = {0, 1, 2, 3, 4, 5, 8};
    const char *labels[] = {"Auto", "1", "2", "3", "4", "5", "Split"};
    for (int i = 0; i < 7; i++) {
      set_choice_(this->vane_h_[this->vane_h_count_++], values[i], labels[i]);
    }
    if (this->swing_) {
      set_choice_(this->vane_h_[this->vane_h_count_++], 12, "Swing");
    }
  }

  this->vane_v_count_ = 0;
  if (this->vane_v_enabled_) {
    const int values[] = {0, 1, 2, 3, 4, 5};
    const char *labels[] = {"Auto", "1", "2", "3", "4", "5"};
    for (int i = 0; i < 6; i++) {
      set_choice_(this->vane_v_[this->vane_v_count_++], values[i], labels[i]);
    }
    if (this->swing_) {
      set_choice_(this->vane_v_[this->vane_v_count_++], 7, "Swing");
    }
  }
}

// The panel scale and the buttons use this window. The heat pump's own
// limits still apply when they sit inside it.
static constexpr float PANEL_TEMP_MIN = 12.0f;
static constexpr float PANEL_TEMP_MAX = 28.0f;

void MelCloud::limits_for_(int mode, float *min_value, float *max_value) const {
  if (mode == 3 || mode == 2) {
    *min_value = this->limits_.min_cool;
    *max_value = this->limits_.max_cool;
  } else if (mode == 8) {
    *min_value = this->limits_.min_auto;
    *max_value = this->limits_.max_auto;
  } else {
    *min_value = this->limits_.min_heat;
    *max_value = this->limits_.max_heat;
  }
  *min_value = std::max(*min_value, PANEL_TEMP_MIN);
  *max_value = std::min(*max_value, PANEL_TEMP_MAX);
  if (*max_value < *min_value) {
    *max_value = *min_value;
  }
}

int MelCloud::mode_code_(const char *mode) {
  if (mode == nullptr) {
    return 1;
  }
  if (std::strcmp(mode, "dry") == 0) {
    return 2;
  }
  if (std::strcmp(mode, "cool") == 0) {
    return 3;
  }
  if (std::strcmp(mode, "auto") == 0) {
    return 8;
  }
  return 1;
}

const char *MelCloud::mode_word_(int mode) {
  switch (mode) {
    case 1:
      return "Heat";
    case 2:
      return "Dry";
    case 3:
      return "Cool";
    case 7:
      return "Fan";
    case 8:
      return "Auto";
    default:
      return "Unknown";
  }
}

void MelCloud::adjust_setpoint(int steps) {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.online || !this->shown_.has_state || steps == 0) {
    return;
  }
  const int mode = this->pending_.mode ? this->pending_.mode_value : this->shown_.mode;
  float min_value = 0;
  float max_value = 0;
  this->limits_for_(mode, &min_value, &max_value);
  const float current = this->pending_.temp ? this->pending_.temp_value : this->shown_.set_temp;
  float next = round_to_step(current + static_cast<float>(steps) * this->limits_.step, this->limits_.step);
  next = std::max(min_value, std::min(max_value, next));
  if (std::fabs(next - current) < 0.01f) {
    return;
  }
  this->pending_.temp = true;
  this->pending_.temp_value = next;
  this->pending_.ready_at = millis() + 1000;
  this->shown_.set_temp = next;
}

void MelCloud::select_mode(const char *mode) {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.online || !this->shown_.has_state || mode == nullptr) {
    return;
  }
  if (std::strcmp(mode, "off") == 0) {
    if (!this->shown_.power && !(this->pending_.power && this->pending_.power_value)) {
      this->pending_.power = false;
      this->pending_.mode = false;
      return;
    }
    this->pending_.power = true;
    this->pending_.power_value = false;
    this->pending_.mode = false;
    this->pending_.ready_at = millis() + 1000;
    this->shown_.power = false;
    return;
  }

  const int code = mode_code_(mode);
  const bool on = this->pending_.power ? this->pending_.power_value : this->shown_.power;
  const int current = this->pending_.mode ? this->pending_.mode_value : this->shown_.mode;
  if (on && current == code) {
    this->pending_.power = false;
    this->pending_.mode = false;
    this->shown_.power = true;
    this->shown_.mode = code;
    return;
  }
  if (!on) {
    this->pending_.power = true;
    this->pending_.power_value = true;
    this->shown_.power = true;
  } else {
    this->pending_.power = false;
  }
  if (current != code) {
    this->pending_.mode = true;
    this->pending_.mode_value = code;
  } else {
    this->pending_.mode = false;
  }
  this->pending_.ready_at = millis() + 1000;
  this->shown_.mode = code;
}

void MelCloud::select_fan(int index) {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.online || index < 0 || index >= this->fan_count_) {
    return;
  }
  const int value = this->fan_[index].value;
  if (value == this->confirmed_.fan) {
    this->pending_.fan = false;
    this->shown_.fan = value;
    return;
  }
  this->pending_.fan = true;
  this->pending_.fan_value = value;
  this->pending_.ready_at = millis() + 1000;
  this->shown_.fan = value;
}

void MelCloud::select_vane_h(int index) {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.online || index < 0 || index >= this->vane_h_count_) {
    return;
  }
  const int value = this->vane_h_[index].value;
  if (value == this->confirmed_.vane_h) {
    this->pending_.vane_h = false;
    this->shown_.vane_h = value;
    return;
  }
  this->pending_.vane_h = true;
  this->pending_.vane_h_value = value;
  this->pending_.ready_at = millis() + 1000;
  this->shown_.vane_h = value;
}

void MelCloud::select_vane_v(int index) {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.online || index < 0 || index >= this->vane_v_count_) {
    return;
  }
  const int value = this->vane_v_[index].value;
  if (value == this->confirmed_.vane_v) {
    this->pending_.vane_v = false;
    this->shown_.vane_v = value;
    return;
  }
  this->pending_.vane_v = true;
  this->pending_.vane_v_value = value;
  this->pending_.ready_at = millis() + 1000;
  this->shown_.vane_v = value;
}

bool MelCloud::has_state() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->shown_.has_state;
}

std::string MelCloud::mode_line() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (this->status_ != LinkStatus::LIVE || !this->shown_.has_state) {
    if (!network::is_connected()) {
      return "Connecting: Wifi";
    }
    return "Connecting: MelCloud";
  }
  if (!this->shown_.power) {
    return "Off";
  }
  return mode_word_(this->shown_.mode);
}

std::string MelCloud::setpoint_text() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (this->status_ != LinkStatus::LIVE || !this->shown_.has_state) {
    return "--";
  }
  return one_decimal(this->shown_.set_temp);
}

std::string MelCloud::room_text() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (this->status_ != LinkStatus::LIVE || !this->shown_.has_state) {
    return "Room --";
  }
  return "Room " + one_decimal(this->shown_.room);
}

static const char *const ICON_POWER = "\uE8AC";
static const char *const ICON_SUN = "\uE430";
static const char *const ICON_SNOW = "\uEB3B";
static const char *const ICON_AUTO = "\uE863";
static const char *const ICON_DROP = "\uE798";
static const char *const ICON_FAN = "\U000F0210";
static const char *const ICON_ARROW = "\uE5C8";
static const char *const ICON_SWING = "\uEA18";
static const char *const ICON_SPLIT = "\uE0B6";

static const uint32_t COLOR_HEAT = 0xF5C518;
static const uint32_t COLOR_COOL = 0x7ECBFF;
static const uint32_t COLOR_GREY = 0xB7C0CA;

bool MelCloud::has_live_state() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->status_ == LinkStatus::LIVE && this->shown_.has_state;
}

const char *MelCloud::mode_icon() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.power) {
    return ICON_POWER;
  }
  switch (this->shown_.mode) {
    case 1:
      return ICON_SUN;
    case 2:
      return ICON_DROP;
    case 3:
      return ICON_SNOW;
    case 7:
      return ICON_FAN;
    case 8:
      return ICON_AUTO;
    default:
      return "";
  }
}

uint32_t MelCloud::mode_icon_color() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (!this->shown_.power) {
    return COLOR_GREY;
  }
  switch (this->shown_.mode) {
    case 1:
      return COLOR_HEAT;
    case 2:
    case 3:
      return COLOR_COOL;
    default:
      return COLOR_GREY;
  }
}

const char *MelCloud::fan_icon(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->fan_count_) {
    return "";
  }
  return ICON_FAN;
}

bool MelCloud::fan_is_auto(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->fan_count_) {
    return false;
  }
  return this->fan_[index].value == 0;
}

float MelCloud::fan_scale(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->fan_count_) {
    return 1.0f;
  }
  switch (this->fan_[index].value) {
    case 1:
      return 0.62f;
    case 2:
      return 0.78f;
    case 3:
      return 0.94f;
    case 4:
      return 1.10f;
    case 5:
      return 1.28f;
    default:
      return 0.9f;
  }
}

const char *MelCloud::current_fan_icon() const {
  return ICON_FAN;
}

bool MelCloud::current_fan_is_auto() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  const int current = this->pending_.fan ? this->pending_.fan_value : this->shown_.fan;
  return current == 0;
}

float MelCloud::current_fan_scale() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  const int current = this->pending_.fan ? this->pending_.fan_value : this->shown_.fan;
  switch (current) {
    case 1:
      return 0.62f;
    case 2:
      return 0.78f;
    case 3:
      return 0.94f;
    case 4:
      return 1.10f;
    case 5:
      return 1.28f;
    default:
      return 0.9f;
  }
}

const char *MelCloud::vane_h_icon(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_h_count_) {
    return "";
  }
  switch (this->vane_h_[index].value) {
    case 0:
      return ICON_AUTO;
    case 8:
      return ICON_SPLIT;
    case 12:
      return ICON_SWING;
    default:
      return ICON_ARROW;
  }
}

int MelCloud::vane_h_angle(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_h_count_) {
    return 0;
  }
  switch (this->vane_h_[index].value) {
    case 1:
      return 90;
    case 2:
      return 67;
    case 3:
      return 45;
    case 4:
      return 22;
    case 12:
      return 45;
    default:
      return 0;
  }
}

const char *MelCloud::vane_v_icon(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_v_count_) {
    return "";
  }
  switch (this->vane_v_[index].value) {
    case 0:
      return ICON_AUTO;
    case 7:
      return ICON_SWING;
    default:
      return ICON_ARROW;
  }
}

int MelCloud::vane_v_angle(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_v_count_) {
    return 0;
  }
  switch (this->vane_v_[index].value) {
    case 1:
      return 180;
    case 2:
      return 225;
    case 3:
      return 270;
    case 4:
      return 315;
    case 7:
      return 45;
    default:
      return 0;
  }
}

std::string MelCloud::status_text() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  switch (this->status_) {
    case LinkStatus::LIVE: {
      if (!this->shown_.has_state) {
        return "Connecting";
      }
      const int current = this->pending_.fan ? this->pending_.fan_value : this->shown_.fan;
      for (int i = 0; i < this->fan_count_; i++) {
        if (this->fan_[i].value == current) {
          return std::string("Fan: ") + this->fan_[i].label;
        }
      }
      char fallback[16];
      std::snprintf(fallback, sizeof(fallback), "Fan: %d", current);
      return fallback;
    }
    case LinkStatus::OFFLINE:
      return "";
    case LinkStatus::LOGIN_FAILED:
      return "Login failed";
    case LinkStatus::CAPTCHA:
      return "Captcha required";
    case LinkStatus::NO_DEVICE:
      return "No device";
    case LinkStatus::CONNECTING:
    default:
      return "Connecting";
  }
}

int MelCloud::arc_min() const { return static_cast<int>(std::round(PANEL_TEMP_MIN * 10)); }

int MelCloud::arc_max() const { return static_cast<int>(std::round(PANEL_TEMP_MAX * 10)); }

int MelCloud::arc_value() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  const int lo = static_cast<int>(std::round(PANEL_TEMP_MIN * 10));
  const int hi = static_cast<int>(std::round(PANEL_TEMP_MAX * 10));
  const int value = static_cast<int>(std::round(this->shown_.set_temp * 10));
  return std::max(lo, std::min(hi, value));
}

int MelCloud::arc_room() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  const int lo = static_cast<int>(std::round(PANEL_TEMP_MIN * 10));
  const int hi = static_cast<int>(std::round(PANEL_TEMP_MAX * 10));
  const int value = static_cast<int>(std::round(this->shown_.room * 10));
  return std::max(lo, std::min(hi, value));
}

int MelCloud::arc_min_unlocked_(int mode) const {
  float min_value = 0;
  float max_value = 0;
  this->limits_for_(mode, &min_value, &max_value);
  return static_cast<int>(std::round(min_value * 10));
}

bool MelCloud::mode_is(const char *mode) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (mode != nullptr && std::strcmp(mode, "off") == 0) {
    return !this->shown_.power;
  }
  return this->shown_.power && this->shown_.mode == mode_code_(mode);
}

int MelCloud::fan_count() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->fan_count_;
}

std::string MelCloud::fan_label(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->fan_count_) {
    return "";
  }
  return this->fan_[index].label;
}

bool MelCloud::fan_selected(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->fan_count_) {
    return false;
  }
  const int current = this->pending_.fan ? this->pending_.fan_value : this->shown_.fan;
  return this->fan_[index].value == current;
}

bool MelCloud::show_vane_h() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->vane_h_enabled_;
}

int MelCloud::vane_h_count() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->vane_h_count_;
}

std::string MelCloud::vane_h_label(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_h_count_) {
    return "";
  }
  return this->vane_h_[index].label;
}

bool MelCloud::vane_h_selected(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_h_count_) {
    return false;
  }
  const int current = this->pending_.vane_h ? this->pending_.vane_h_value : this->shown_.vane_h;
  return this->vane_h_[index].value == current;
}

bool MelCloud::show_vane_v() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->vane_v_enabled_;
}

int MelCloud::vane_v_count() const {
  std::lock_guard<std::mutex> lock(this->mu_);
  return this->vane_v_count_;
}

std::string MelCloud::vane_v_label(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_v_count_) {
    return "";
  }
  return this->vane_v_[index].label;
}

bool MelCloud::vane_v_selected(int index) const {
  std::lock_guard<std::mutex> lock(this->mu_);
  if (index < 0 || index >= this->vane_v_count_) {
    return false;
  }
  const int current = this->pending_.vane_v ? this->pending_.vane_v_value : this->shown_.vane_v;
  return this->vane_v_[index].value == current;
}

}  // namespace melcloud
}  // namespace esphome
