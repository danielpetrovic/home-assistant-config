#include "iohc_mode_select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace iohc {

static const char *const TAG = "iohc.mode_select";

void IOHCModeSelect::setup() { this->publish_state(static_cast<size_t>(cover_->get_mode())); }

void IOHCModeSelect::control(size_t index) {
  cover_->set_mode(static_cast<IOHCCover::Mode>(index));
  this->publish_state(index);
}

void IOHCModeSelect::dump_config() { LOG_SELECT("", "Somfy IOHC Mode Select", this); }

}  // namespace iohc
}  // namespace esphome
