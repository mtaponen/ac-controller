#pragma once

#include "esphome/components/melcloud/melcloud.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

#include <memory>

namespace esphome {
namespace setup_portal {

class DNSServer;

class SetupPortal : public AsyncWebHandler, public Component {
 public:
  explicit SetupPortal(web_server_base::WebServerBase *base) : base_(base) {}
  ~SetupPortal() override;

  void set_melcloud(melcloud::MelCloud *melcloud) { this->melcloud_ = melcloud; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  void watch_button_();
  void load_state_();
  void save_state_();
  void adopt_wifi_saved_();
  void erase_wifi_sta_();
  void watch_attempts_();
  void give_up_();
  void mark_ok_();
  void start_portal_();
  void handle_save_(AsyncWebServerRequest *request);
  std::string form_html_() const;

  web_server_base::WebServerBase *base_;
  melcloud::MelCloud *melcloud_{nullptr};
  std::unique_ptr<DNSServer> dns_;
  bool handlers_added_{false};
  bool active_{false};
  uint8_t phase_{0};
  char ssid_[33]{};
  char wifi_password_[65]{};
  uint8_t attempts_{0};
  uint32_t window_start_{0};
};

}  // namespace setup_portal
}  // namespace esphome
