#include "iohc_cover.h"
#include "esphome/core/log.h"
#include "../iohcCryptoHelpers.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace esphome {
namespace iohc {

static const char *const TAG = "iohc.cover";

// ESP32 NVS namespace names are capped at 15 chars - hash the nvs_key down
// to a short, fixed, deterministic one, same approach as iohc_remote1w.cpp.
static std::string cover_nvs_namespace_for(const std::string &nvs_key) {
  size_t h = std::hash<std::string>{}(nvs_key);
  char buf[11];
  snprintf(buf, sizeof(buf), "ic%08x", static_cast<unsigned>(h));
  return std::string(buf);
}

void IOHCCover::setup() {
  remote_.set_type(type_);
  remote_.set_manufacturer(manufacturer_);
  // Bonded identity/sequence persist per-cover (see iohc_remote1w.cpp) -
  // nvs_key_ is only used to derive a stable NVS namespace, not as a
  // protocol address.
  remote_.begin(IOHC::iohcRadio::getInstance(), this->nvs_key_, fixed_node_hex_, fixed_key_hex_);

  cover_prefs_namespace_ = cover_nvs_namespace_for(this->nvs_key_);
  cover_prefs_.begin(cover_prefs_namespace_.c_str(), false);
  mode_ = static_cast<Mode>(cover_prefs_.getUChar("mode", static_cast<uint8_t>(Mode::POSITION)));

  remote_.set_travel_time_open(TRAVEL_TIME_OPEN);
  remote_.set_travel_time_close(TRAVEL_TIME_CLOSE);

  // Restore last known position (defaults to open if never set).
  this->position = cover_prefs_.getFloat("position", 1.0f);
  if (mode_ == Mode::POSITION) {
    remote_.position_tracker().setPosition(this->position * 100.0f);
  }

  if (has_motor_address_) {
    parent_->register_cover_for_position_updates(motor_address_, this);
  }

  if (target_closure_sensor_ != nullptr && cover_prefs_.isKey("target_closure")) {
    target_closure_sensor_->publish_state(cover_prefs_.getFloat("target_closure", 0.0f));
  }
}

void IOHCCover::set_motor_address(const std::string &motor_address_hex) {
  if (motor_address_hex.empty())
    return;
  hexStringToBytes(motor_address_hex, motor_address_);
  has_motor_address_ = true;
}

void IOHCCover::update_real_position(float closure_percent) {
  // Only touches the standalone sensor, never this->position/
  // current_operation/the tracker - passively decoded frames aren't
  // validated (no CRC in the RX pipeline), so a bad decode must not be able
  // to corrupt the entity actually used for control.
  closure_percent = std::clamp(closure_percent, 0.0f, 100.0f);
  ESP_LOGI(TAG, "Target Closure sensor updated from passive 2W decode: %.0f%%", closure_percent);
  if (target_closure_sensor_ != nullptr) {
    target_closure_sensor_->publish_state(closure_percent);
  }
  cover_prefs_.putFloat("target_closure", closure_percent);
}

void IOHCCover::set_mode(Mode mode) {
  mode_ = mode;
  cover_prefs_.putUChar("mode", static_cast<uint8_t>(mode));
}

void IOHCCover::loop() {
  if (mode_ != Mode::POSITION)
    return; // MY/TWO_WAY don't tick a time-based position estimate

  auto &tracker = remote_.position_tracker();
  tracker.update();

  // BlindPosition only knows how to run to a full extreme (0 or 100) - it
  // has no concept of an intermediate target. Without this check the local
  // estimate would keep running to 0%/100% regardless of what was actually
  // requested, even though the real motor stops correctly on its own.
  if (tracker.isMoving() && target_position_ >= 0.0f) {
    bool reached_opening =
        this->current_operation == cover::COVER_OPERATION_OPENING && tracker.getPosition() >= target_position_;
    bool reached_closing =
        this->current_operation == cover::COVER_OPERATION_CLOSING && tracker.getPosition() <= target_position_;
    if (reached_opening || reached_closing) {
      tracker.setPosition(target_position_);
      tracker.stop();
    }
  }

  // Round to the nearest whole percent - the real motor's own resolution
  // (confirmed via passive 2W decode) is always a whole percent, so a
  // finer-grained local estimate is just noise (extra publish_state()/log
  // spam) with no real precision behind it.
  float pos = std::round(tracker.getPosition()) / 100.0f;
  bool position_changed = std::fabs(pos - this->position) > 0.001f;
  bool now_idle = !tracker.isMoving() && this->current_operation != cover::COVER_OPERATION_IDLE;

  if (!position_changed && !now_idle)
    return;

  this->position = pos;
  if (now_idle)
    this->current_operation = cover::COVER_OPERATION_IDLE;
  this->publish_state();
}

void IOHCCover::dump_config() {
  LOG_COVER("", "Somfy IOHC Cover", this);
  ESP_LOGCONFIG(TAG, "  Paired: %s", remote_.is_paired() ? "yes" : "no");
  const char *mode_name = mode_ == Mode::POSITION ? "Position" : mode_ == Mode::MY ? "Open / My / Close" : "Two-Way (Soon)";
  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_name);
  ESP_LOGCONFIG(TAG, "  Travel time open/close: %us / %us (fixed)", TRAVEL_TIME_OPEN, TRAVEL_TIME_CLOSE);
}

cover::CoverTraits IOHCCover::get_traits() {
  auto traits = cover::CoverTraits();
  // Traits stay constant across all 3 modes (position support always on) so
  // switching modes at runtime doesn't require HA to re-discover the entity.
  traits.set_is_assumed_state(true);
  traits.set_supports_position(true);
  traits.set_supports_toggle(false);
  traits.set_supports_stop(true);
  return traits;
}

void IOHCCover::press_my() {
  remote_.cmd(IOHC::RemoteButton::Vent);
  if (mode_ == Mode::MY) {
    this->position = 0.5f;
    cover_prefs_.putFloat("position", 0.5f);
  } else {
    target_position_ = -1.0f;
  }
  this->current_operation = cover::COVER_OPERATION_IDLE;
  this->publish_state();
}

void IOHCCover::press_prog2w() {
  if (!has_motor_address_) {
    ESP_LOGW(TAG, "Program (2W) pressed but this cover has no motor_address configured - see README's Real "
                  "position feedback section for how to look it up. Refusing to arm (will not listen for just "
                  "any DISCOVER broadcast).");
    return;
  }
  parent_->controller2w().arm_bonding(motor_address_, this);
}

void IOHCCover::control(const cover::CoverCall &call) {
  if (mode_ == Mode::TWO_WAY) {
    ESP_LOGW(TAG, "Two-Way mode selected but not implemented yet - command ignored");
    return;
  }

  if (call.get_stop()) {
    remote_.cmd(IOHC::RemoteButton::Stop);
    if (mode_ == Mode::MY) {
      this->position = 0.5f;
      cover_prefs_.putFloat("position", 0.5f);
    } else {
      target_position_ = -1.0f;
      cover_prefs_.putFloat("position", remote_.position_tracker().getPosition() / 100.0f);
    }
    this->current_operation = cover::COVER_OPERATION_IDLE;
    this->publish_state();
    return;
  }

  if (!call.get_position().has_value())
    return;

  float target = *call.get_position(); // 0 (closed) .. 1 (open)
  int percent = static_cast<int>(lroundf(target * 100.0f));

  if (mode_ == Mode::MY) {
    // 3 discrete states, any intermediate request maps to the "My"/favorite
    // position. Unlike RTS (where My and Stop are genuinely the same
    // physical button), IO's real "My" is a distinct command (Vent,
    // main=0xd8) from Stop (main=0xd2) - confirmed via a live capture of a
    // real TaHoma "My" press. Stop only does something while the motor is
    // actively moving, which is why this previously did nothing when the
    // cover was already idle. The dedicated Stop button above (call.get_stop())
    // still sends real Stop, for interrupting an in-progress move.
    if (percent >= 100) {
      remote_.cmd(IOHC::RemoteButton::Open);
      this->position = 1.0f;
    } else if (percent <= 0) {
      remote_.cmd(IOHC::RemoteButton::Close);
      this->position = 0.0f;
    } else {
      remote_.cmd(IOHC::RemoteButton::Vent);
      this->position = 0.5f;
    }
    cover_prefs_.putFloat("position", this->position);
    this->current_operation = cover::COVER_OPERATION_IDLE;
    this->publish_state();
    return;
  }

  // Mode::POSITION
  if (percent >= 100) {
    remote_.cmd(IOHC::RemoteButton::Open);
    target_position_ = 100.0f;
    this->current_operation = cover::COVER_OPERATION_OPENING;
  } else if (percent <= 0) {
    remote_.cmd(IOHC::RemoteButton::Close);
    target_position_ = 0.0f;
    this->current_operation = cover::COVER_OPERATION_CLOSING;
  } else {
    float current = remote_.position_tracker().getPosition();
    remote_.cmd(IOHC::RemoteButton::Position, percent);
    target_position_ = static_cast<float>(percent);
    this->current_operation =
        (percent > current) ? cover::COVER_OPERATION_OPENING : cover::COVER_OPERATION_CLOSING;
  }
  cover_prefs_.putFloat("position", target_position_ / 100.0f);
  this->publish_state();
}

}  // namespace iohc
}  // namespace esphome
