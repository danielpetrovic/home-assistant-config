#include "iohc_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace iohc {

static const char *const TAG = "iohc.button";

void IOHCPairButton::press_action() {
  const char *name = type_ == IOHC::RemoteButton::Add     ? "Add"
                     : type_ == IOHC::RemoteButton::Pair  ? "Pair"
                     : "Remove";
  ESP_LOGI(TAG, "%s pressed - transmitting %s", this->get_name().c_str(), name);
  cover_->remote().cmd(type_);
}

void IOHCPairButton::dump_config() { ESP_LOGCONFIG(TAG, "Somfy IOHC Pair Button '%s'", this->get_name().c_str()); }

}  // namespace iohc
}  // namespace esphome
