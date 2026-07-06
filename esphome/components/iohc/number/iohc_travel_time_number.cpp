#include "iohc_travel_time_number.h"

namespace esphome {
namespace iohc {

void IOHCTravelTimeNumber::setup() {
  uint32_t seconds =
      (type_ == Type::OPEN) ? cover_->get_travel_time_open() : cover_->get_travel_time_close();
  this->publish_state(static_cast<float>(seconds));
}

void IOHCTravelTimeNumber::control(float value) {
  auto seconds = static_cast<uint32_t>(value);
  if (type_ == Type::OPEN) {
    cover_->set_travel_time_open_and_persist(seconds);
  } else {
    cover_->set_travel_time_close_and_persist(seconds);
  }
  this->publish_state(value);
}

}  // namespace iohc
}  // namespace esphome
