#include "iohc_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace iohc {

static const char *const TAG = "iohc.button";

void IOHCPairButton::press_action() {
  const char *name = type_ == IOHC::RemoteButton::Prog            ? "Prog"
                      : type_ == IOHC::RemoteButton::Add          ? "Add"
                      : type_ == IOHC::RemoteButton::Pair          ? "Pair"
                      : type_ == IOHC::RemoteButton::Remove        ? "Remove"
                      : type_ == IOHC::RemoteButton::Vent          ? "My"
                      : type_ == IOHC::RemoteButton::Identify      ? "Identify"
                      : type_ == IOHC::RemoteButton::StartIdentify ? "Start Identify"
                                                                    : "Stop Identify";
  ESP_LOGI(TAG, "%s pressed - transmitting %s", this->get_name().c_str(), name);
  // My also needs the cover's own displayed position/state updated (not
  // just the radio command fired), unlike every other button type here -
  // see IOHCCover::press_my(). Prog resolves to Add or Remove internally
  // (see IOHCRemote1W::cmd()'s RemoteButton::Prog case) - the actual command
  // sent is logged there, not here.
  if (type_ == IOHC::RemoteButton::Vent) {
    cover_->press_my();
  } else {
    cover_->remote().cmd(type_);
  }
}

void IOHCPairButton::dump_config() { ESP_LOGCONFIG(TAG, "Somfy IOHC Pair Button '%s'", this->get_name().c_str()); }

}  // namespace iohc
}  // namespace esphome
