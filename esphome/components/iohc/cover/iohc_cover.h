#pragma once

#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "../iohc.h"
#include "../iohc_remote1w.h"
#include <Preferences.h>

namespace esphome {
namespace iohc {

class IOHCCover : public cover::Cover, public Component {
 public:
  // POSITION (default): sends real percentage targets to the motor over 1W,
  //   which reliably lands exactly where commanded. The displayed position
  //   between send and arrival is a local BlindPosition travel-time
  //   estimate - purely cosmetic (animates "still moving" in HA), clamped to
  //   land exactly on the commanded target regardless of timing drift.
  // MY: matches the RTS bridge's own default model exactly - 3 discrete
  //   states (0.0 closed / 0.5 MY-or-stopped / 1.0 open), no time tracking.
  //   Any position request strictly between 0 and 1 maps to the physical
  //   MY/Stop button (cmd main=0xd2), same button either way. The only mode
  //   that does NOT send arbitrary percentage targets.
  // TWO_WAY: real motor-reported position via the 2W challenge/response
  //   layer - not implemented yet, selecting it just logs a warning and
  //   ignores commands.
  enum class Mode : uint8_t { POSITION = 0, MY = 1, TWO_WAY = 2 };

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  cover::CoverTraits get_traits() override;

  void set_parent(IOHCComponent *parent) { parent_ = parent; }
  void set_type(uint8_t type) { type_ = type; }
  void set_manufacturer(uint8_t manufacturer) { manufacturer_ = manufacturer; }
  // Optional (6/32 hex chars). If both set, the bonded identity comes from
  // YAML/secrets.yaml instead of being randomly generated into this board's
  // own flash - see IOHC::IOHCRemote1W::begin() for why.
  void set_fixed_node(const std::string &node_hex) { fixed_node_hex_ = node_hex; }
  void set_fixed_key(const std::string &key_hex) { fixed_key_hex_ = key_hex; }
  // The cover's own YAML component id (e.g. "garden_shutter_io"), NOT
  // get_object_id() - deliberately decoupled from HA's entity/device naming.
  // Used only to derive a stable NVS namespace for bonded identity + persisted
  // cover state (position/mode). get_object_id() depends on the entity's
  // name/device_id assignment, which is a presentation choice that can change
  // (e.g. switching a cover to its own HA sub-device) without us intending to
  // orphan already-bonded motors or reset their sequence counters.
  void set_nvs_key(const std::string &key) { nvs_key_ = key; }

  IOHC::IOHCRemote1W &remote() { return remote_; }

  Mode get_mode() const { return mode_; }
  void set_mode(Mode mode);

  // Dedicated My button entry point (as opposed to Prog/Identify, which only
  // need to fire the radio command). My also has to update the cover's own
  // displayed position/state, same as the position-slider path in control()
  // does - otherwise the motor moves but Home Assistant keeps showing the
  // stale position. Mirrors the get_stop() branch in control() exactly,
  // just sending Vent instead of Stop.
  void press_my();

  // "Program (2W)" button entry point (Phase 3) - arms this bridge's shared
  // IOHC::IOHCController2W to bond with this cover's own motor_address.
  // Requires motor_address to be set (see set_motor_address()) - fails fast
  // with a log warning otherwise, never falls back to "listen for any
  // motor's DISCOVER broadcast" (see the plan file's "scoping DISCOVER
  // matches" rationale - a real security consideration, not just tidiness).
  void press_prog2w();

  // "Get Name (2W)" button entry point - opt-in diagnostic, not part of
  // bonding itself. Requires this motor to already be bonded (Program (2W)
  // first) - see IOHC::IOHCController2W::send_get_name()'s own comment.
  void press_get_name2w();

  // Real motor address (6 hex chars) - as assigned by Somfy, NOT this
  // bridge's own 1W virtual remote identity (node/key above, which is a
  // separate, locally-generated address). Optional: without it, this cover
  // just never receives passive position updates - see README's "Real
  // position feedback" section for how to look this up (Overkiz's own
  // unique_id, if you have TaHoma/Connexoon) and the hard requirement that
  // an existing 2W-bonded controller (TaHoma or similar) must already exist
  // and be polling for this to produce anything at all.
  void set_motor_address(const std::string &motor_address_hex);
  // Called by IOHCComponent::on_receive() when a real, motor-reported
  // closure value (0=open/100=closed, per core:ClosureState) is decoded
  // from passively overheard 2W traffic - see iohc.cpp. Only updates the
  // standalone Target Closure sensor, never this cover's own position/
  // current_operation/mode: passively decoded frames aren't validated (no
  // CRC in the RX pipeline), so a garbled reception could otherwise corrupt
  // the entity actually used for control.
  void update_real_position(float closure_percent);
  // Phase 3d counterpart to update_real_position() above - called only from
  // IOHCController2W's own send_command() completion (iohc_controller2w.cpp),
  // never from the generic passive-decode path. Unlike the passive version,
  // this IS allowed to update the cover's real position/current_operation:
  // the frame it comes from is actively solicited by our own authenticated
  // command and verified via the live 0x3C/0x3D challenge/response, not an
  // unvalidated overheard frame - see Finding 10's "never let an unvalidated
  // side-channel corrupt the primary entity" rule, which this deliberately
  // does not fall under.
  void update_real_position_authoritative(float closure_percent);
  // Optional standalone sensor (see sensor/__init__.py) - published in the
  // same closure % convention as update_real_position()'s own parameter, so
  // it matches HA's Overkiz "Target closure" sensor exactly rather than this
  // cover's own position attribute (which uses HA's inverted 0=closed/1=open
  // cover convention). Never published to until real data actually arrives.
  void set_target_closure_sensor(sensor::Sensor *s) { target_closure_sensor_ = s; }
  // Called by IOHCComponent::on_receive() for EVERY frame (any command, 1W
  // or 2W) whose source address matches this cover's motor_address - unlike
  // update_real_position(), this is not decoded frame content, just this
  // board's own radio's RSSI measurement for that reception, so there's
  // nothing to validate/corrupt: a misattributed frame (wrong address
  // decoded) would at worst show a plausible-but-wrong signal number, not a
  // wrong cover state. This is OUR bridge's own view of the motor's signal
  // (distinct from Overkiz's "RSSI Level" sensor, which reflects TaHoma's
  // own radio - see the "Real position feedback" section in the README).
  void update_last_rssi(float rssi_dbm);
  void set_last_rssi_sensor(sensor::Sensor *s) { last_rssi_sensor_ = s; }

 protected:
  void control(const cover::CoverCall &call) override;

  IOHCComponent *parent_{};
  IOHC::IOHCRemote1W remote_;
  // Fixed, not user-configurable - purely cosmetic (see Mode::POSITION).
  static constexpr uint32_t TRAVEL_TIME_OPEN = 25;
  static constexpr uint32_t TRAVEL_TIME_CLOSE = 25;
  uint8_t type_{0};
  uint8_t manufacturer_{2};
  std::string fixed_node_hex_;
  std::string fixed_key_hex_;
  std::string nvs_key_;
  IOHC::address motor_address_{};
  bool has_motor_address_{false};
  sensor::Sensor *target_closure_sensor_{nullptr};
  sensor::Sensor *last_rssi_sensor_{nullptr};

  Mode mode_{Mode::POSITION};
  // Explicitly global-scoped: inside esphome::iohc, unqualified "Preferences"
  // resolves to esphome's OWN esphome::Preferences (aka esp32::ESP32Preferences,
  // pulled in transitively via esphome/core/preferences.h through cover.h) -
  // a completely different class with no begin()/getUChar()/etc. Arduino's
  // global ::Preferences (from <Preferences.h>) is what we actually want,
  // same one iohc_remote1w.h uses without issue since that file lives in the
  // separate global namespace IOHC, not under esphome::, so no collision there.
  ::Preferences cover_prefs_;
  std::string cover_prefs_namespace_;

  // Percent (0-100, ESPHome convention: 100=open) the local BlindPosition
  // estimate should stop at. BlindPosition itself only knows how to run to a
  // full extreme (0 or 100) - upstream's own iohcRemote1W has separate
  // target-tracking logic (remote.targetPosition + updatePositions()) that
  // wasn't ported into IOHCRemote1W, so without this the software estimate
  // would run all the way to 0/100 regardless of what was actually
  // requested, even though the real motor stops correctly on its own.
  // -1 means "no target set" (Stop was called, or nothing requested yet).
  // Only meaningful in Mode::POSITION.
  float target_position_{-1.0f};
};

}  // namespace iohc
}  // namespace esphome
