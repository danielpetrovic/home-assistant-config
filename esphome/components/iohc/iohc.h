#pragma once

// Thin ESPHome wrapper around the vendored io-homecontrol radio stack (the
// other files in this same directory - ported from
// https://github.com/rspaargaren/iohomecontrol, Apache-2.0). Flat layout is
// required: ESPHome's external_components resource discovery only looks at
// a component's own top-level directory, not subdirectories (confirmed in
// esphome/loader.py's Component.resources - "does not look through
// subdirectories"), so a vendor/ subfolder silently doesn't get copied into
// the build at all. This component owns none of the radio logic itself - it
// just starts IOHC::iohcRadio on ESPHome's setup() and logs whatever comes
// in. Phase 0/1 scope: prove real reception, single fixed channel, no
// TX/pairing.

#include "esphome/core/component.h"
#include "iohcRadio.h"
#include <unordered_map>

namespace esphome {
namespace iohc {

class IOHCCover;

class IOHCComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool on_receive(IOHC::iohcPacket *packet);

  // For the OLED status page.
  uint32_t get_packets_received() const { return packets_received_; }
  float get_last_rssi() const { return last_rssi_; }

  // Passive 2W position sync (see README's "Real position feedback" section).
  // motor_address is the shutter's REAL address - as assigned by Somfy at
  // pairing time - packed the same way as address_rssi_'s keys. Called by
  // IOHCCover::setup() when motor_address is configured.
  void register_cover_for_position_updates(const IOHC::address &motor_address, IOHCCover *cover);

 protected:
  uint32_t packets_received_{0};
  float last_rssi_{0};

  // Per-source-address RSSI, logged (not exposed as HA entities) - addresses
  // aren't known ahead of time and motors never transmit to us in 1W mode,
  // so a fixed per-cover sensor would either be empty or a duplicate of
  // whichever device was last heard. Packed as (source[0]<<16 | source[1]<<8
  // | source[2]) - see on_receive().
  std::unordered_map<uint32_t, float> address_rssi_;

  // Motor address (packed the same way) -> the cover to notify when a real
  // position for that motor is passively decoded. Only covers with
  // motor_address set are registered here.
  std::unordered_map<uint32_t, IOHCCover *> position_covers_;
};

}  // namespace iohc
}  // namespace esphome
