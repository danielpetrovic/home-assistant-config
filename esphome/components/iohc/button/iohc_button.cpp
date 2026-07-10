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
                      : type_ == IOHC::RemoteButton::StopIdentify  ? "Stop Identify"
                                                                    : "Prog (2W)";
  ESP_LOGI(TAG, "%s pressed - transmitting %s", this->get_name().c_str(), name);
  // My and Prog2W both need cover-level handling (position/state update for
  // My, arming the shared 2W controller for Prog2W), unlike every other
  // button type here which just fires the 1W radio command directly - see
  // IOHCCover::press_my() / press_prog2w(). Prog (1W) resolves to Add or
  // Remove internally (see IOHCRemote1W::cmd()'s RemoteButton::Prog case) -
  // the actual command sent is logged there, not here.
  if (type_ == IOHC::RemoteButton::Vent) {
    cover_->press_my();
  } else if (type_ == IOHC::RemoteButton::Prog2W) {
    cover_->press_prog2w();
  } else {
    cover_->remote().cmd(type_);
  }
}

void IOHCPairButton::dump_config() { ESP_LOGCONFIG(TAG, "Somfy IOHC Pair Button '%s'", this->get_name().c_str()); }

}  // namespace iohc
}  // namespace esphome
