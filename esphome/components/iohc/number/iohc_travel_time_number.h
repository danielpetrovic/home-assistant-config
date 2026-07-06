#pragma once

#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "../cover/iohc_cover.h"

namespace esphome {
namespace iohc {

class IOHCTravelTimeNumber : public number::Number, public Component {
 public:
  enum class Type : uint8_t { OPEN = 0, CLOSE = 1 };

  void setup() override;
  // After the cover's own DATA priority, matching IOHCModeSelect - the
  // cover's persisted travel times must already be loaded (setup() there
  // overrides the YAML default with a persisted value) before this entity
  // reads them to publish its initial state.
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void set_cover(IOHCCover *cover) { cover_ = cover; }
  void set_travel_time_type(Type type) { type_ = type; }

 protected:
  void control(float value) override;

  IOHCCover *cover_{};
  Type type_{Type::OPEN};
};

}  // namespace iohc
}  // namespace esphome
