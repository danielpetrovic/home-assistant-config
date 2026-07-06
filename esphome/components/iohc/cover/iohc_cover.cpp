#include "iohc_cover.h"
#include "esphome/core/log.h"
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
  mode_ = static_cast<Mode>(cover_prefs_.getUChar("mode", static_cast<uint8_t>(Mode::MY)));

  // Travel times: the YAML-configured value (already loaded into
  // travel_time_open_/close_ via set_travel_time_open()/close() at codegen
  // time) is only the initial default - a value persisted by the Travel
  // Time Open/Close number entities wins if present, so a runtime change
  // from HA survives a reboot without needing a reflash.
  travel_time_open_ = cover_prefs_.getUInt("tt_open", travel_time_open_);
  travel_time_close_ = cover_prefs_.getUInt("tt_close", travel_time_close_);
  remote_.set_travel_time_open(travel_time_open_);
  remote_.set_travel_time_close(travel_time_close_);

  if (mode_ == Mode::TIMED) {
    this->position = remote_.position_tracker().getPosition() / 100.0f;
  } else {
    // MY / TWO_WAY: discrete, persisted position - defaults to open (1.0)
    // when nothing is known yet, matching the RTS bridge's own default.
    this->position = cover_prefs_.getFloat("position", 1.0f);
  }
}

void IOHCCover::set_mode(Mode mode) {
  mode_ = mode;
  cover_prefs_.putUChar("mode", static_cast<uint8_t>(mode));
}

void IOHCCover::set_travel_time_open_and_persist(uint32_t seconds) {
  travel_time_open_ = seconds;
  remote_.set_travel_time_open(seconds);
  cover_prefs_.putUInt("tt_open", seconds);
}

void IOHCCover::set_travel_time_close_and_persist(uint32_t seconds) {
  travel_time_close_ = seconds;
  remote_.set_travel_time_close(seconds);
  cover_prefs_.putUInt("tt_close", seconds);
}

void IOHCCover::loop() {
  if (mode_ != Mode::TIMED)
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

  float pos = tracker.getPosition() / 100.0f;
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
  const char *mode_name = mode_ == Mode::TIMED ? "1W Timed" : mode_ == Mode::MY ? "1W My" : "2W";
  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_name);
  ESP_LOGCONFIG(TAG, "  Travel time open/close: %us / %us", travel_time_open_, travel_time_close_);
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

void IOHCCover::control(const cover::CoverCall &call) {
  if (mode_ == Mode::TWO_WAY) {
    ESP_LOGW(TAG, "2W mode selected but not implemented yet - command ignored");
    return;
  }

  if (call.get_stop()) {
    remote_.cmd(IOHC::RemoteButton::Stop);
    if (mode_ == Mode::MY) {
      this->position = 0.5f;
      cover_prefs_.putFloat("position", 0.5f);
    } else {
      target_position_ = -1.0f;
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
    // Matches the RTS bridge's own model exactly: 3 discrete states, any
    // intermediate request maps to the physical MY/Stop button - no
    // time-based estimation, no drift.
    if (percent >= 100) {
      remote_.cmd(IOHC::RemoteButton::Open);
      this->position = 1.0f;
    } else if (percent <= 0) {
      remote_.cmd(IOHC::RemoteButton::Close);
      this->position = 0.0f;
    } else {
      remote_.cmd(IOHC::RemoteButton::Stop);
      this->position = 0.5f;
    }
    cover_prefs_.putFloat("position", this->position);
    this->current_operation = cover::COVER_OPERATION_IDLE;
    this->publish_state();
    return;
  }

  // Mode::TIMED
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
  this->publish_state();
}

}  // namespace iohc
}  // namespace esphome
