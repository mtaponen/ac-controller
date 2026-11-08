#pragma once

#include "esphome/core/component.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace esphome {
namespace melcloud {

class MelCloud : public Component {
 public:
  bool save_credentials(const std::string &email, const std::string &password, const std::string &device_id);
  const std::string &email() const { return this->email_; }
  const std::string &password() const { return this->password_; }
  const std::string &wanted_device_id() const { return this->wanted_device_id_; }
  bool has_device() const;
  void kick_link();
  void request_refresh();

  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override;

  void adjust_setpoint(int steps);
  void select_mode(const char *mode);
  void select_fan(int index);
  void select_vane_h(int index);
  void select_vane_v(int index);

  bool has_state() const;
  std::string mode_line() const;
  std::string setpoint_text() const;
  std::string room_text() const;
  std::string status_text() const;
  bool has_live_state() const;
  const char *mode_icon() const;
  uint32_t mode_icon_color() const;
  const char *current_fan_icon() const;
  float current_fan_scale() const;
  bool current_fan_is_auto() const;
  const char *fan_icon(int index) const;
  float fan_scale(int index) const;
  bool fan_is_auto(int index) const;
  const char *vane_h_icon(int index) const;
  int vane_h_angle(int index) const;
  const char *vane_v_icon(int index) const;
  int vane_v_angle(int index) const;

  int arc_min() const;
  int arc_max() const;
  int arc_value() const;
  int arc_room() const;

  bool mode_is(const char *mode) const;

  int fan_count() const;
  std::string fan_label(int index) const;
  bool fan_selected(int index) const;

  bool show_vane_h() const;
  int vane_h_count() const;
  std::string vane_h_label(int index) const;
  bool vane_h_selected(int index) const;

  bool show_vane_v() const;
  int vane_v_count() const;
  std::string vane_v_label(int index) const;
  bool vane_v_selected(int index) const;

 protected:
  struct Reading {
    bool has_state{false};
    bool online{false};
    bool power{false};
    int mode{1};
    float set_temp{20};
    float room{0};
    int fan{0};
    int vane_h{0};
    int vane_v{0};
  };

  struct Pending {
    bool temp{false};
    float temp_value{0};
    bool power{false};
    bool power_value{false};
    bool mode{false};
    int mode_value{1};
    bool fan{false};
    int fan_value{0};
    bool vane_h{false};
    int vane_h_value{0};
    bool vane_v{false};
    int vane_v_value{0};
    uint32_t ready_at{0};
  };

  struct Limits {
    float step{0.5f};
    float min_heat{10};
    float max_heat{31};
    float min_cool{16};
    float max_cool{31};
    float min_auto{16};
    float max_auto{31};
  };

  struct Choice {
    int value{0};
    char label[12]{};
  };

  enum class LinkStatus {
    CONNECTING,
    LIVE,
    OFFLINE,
    LOGIN_FAILED,
    CAPTCHA,
    NO_DEVICE,
  };

  void load_credentials_();
  void task_loop_();
  static void task_thunk_(void *param);
  bool login_();
  bool list_devices_();
  bool poll_(bool revert_pending);
  bool write_one_();
  void ingest_state_(const std::string &json, bool from_server);
  void rebuild_options_();
  void limits_for_(int mode, float *min_value, float *max_value) const;
  void revert_pending_();
  bool any_pending_unlocked_() const;
  int arc_min_unlocked_(int mode) const;
  static void set_choice_(Choice &choice, int value, const char *label);
  static int mode_code_(const char *mode);
  static const char *mode_word_(int mode);

  std::string email_;
  std::string password_;
  std::string wanted_device_id_;
  std::string token_;
  std::string device_id_;
  std::string building_id_;
  std::string device_name_;
  std::string last_state_;

  Reading confirmed_{};
  Reading shown_{};
  Pending pending_{};
  Limits limits_{};
  LinkStatus status_{LinkStatus::CONNECTING};
  bool caps_ready_{false};
  bool auto_fan_{false};
  bool vane_h_enabled_{false};
  bool vane_v_enabled_{false};
  bool swing_{false};
  int fan_speeds_{0};

  int fan_count_{0};
  Choice fan_[6]{};
  int vane_h_count_{0};
  Choice vane_h_[8]{};
  int vane_v_count_{0};
  Choice vane_v_[8]{};

  uint32_t next_poll_ms_{0};
  uint32_t next_write_ms_{0};
  uint32_t next_attempt_ms_{0};
  uint32_t last_poll_ok_ms_{0};
  bool have_poll_ok_{false};
  bool poll_now_{false};
  bool login_blocked_{false};
  bool device_ready_{false};
  mutable std::mutex mu_;
};

}  // namespace melcloud
}  // namespace esphome
