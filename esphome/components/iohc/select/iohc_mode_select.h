#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../cover/iohc_cover.h"

namespace esphome {
namespace iohc {

class IOHCModeSelect : public select::Select, public Component {
 public:
  void setup() override;
  void dump_config() override;
  // Runs after the cover's own setup() (DATA priority) so the cover has
  // already loaded its persisted mode from Preferences before this reads it.
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void set_cover(IOHCCover *cover) { cover_ = cover; }

 protected:
  void control(size_t index) override;

  IOHCCover *cover_{};
};

}  // namespace iohc
}  // namespace esphome
