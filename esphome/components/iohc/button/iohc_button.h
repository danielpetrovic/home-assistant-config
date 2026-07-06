#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "../cover/iohc_cover.h"

namespace esphome {
namespace iohc {

class IOHCPairButton : public button::Button, public Component {
 public:
  void dump_config() override;

  void set_cover(IOHCCover *cover) { cover_ = cover; }
  void set_button_type(IOHC::RemoteButton type) { type_ = type; }

 protected:
  void press_action() override;

  IOHCCover *cover_{};
  IOHC::RemoteButton type_{IOHC::RemoteButton::Pair};
};

}  // namespace iohc
}  // namespace esphome
