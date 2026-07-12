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
#include "iohc_controller2w.h"
#include <string>
#include <unordered_map>

namespace esphome {
namespace iohc {

class IOHCCover;

class IOHCComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool on_receive(IOHC::iohcPacket *packet);

  // Passive 2W position sync (see README's "Real position feedback" section).
  // motor_address is the shutter's REAL address - as assigned by Somfy at
  // pairing time - packed the same way as address_rssi_'s keys. Called by
  // IOHCCover::setup() when motor_address is configured.
  void register_cover_for_position_updates(const IOHC::address &motor_address, IOHCCover *cover);

  // This bridge's own 2W bonding/control (Phase 3) - one shared instance for
  // the whole bridge, see iohc_controller2w.h. Distinct from the passive
  // decode above (which just reads another already-bonded controller's
  // traffic) - this is the bridge actively bonding with and commanding a
  // motor itself.
  IOHC::IOHCController2W &controller2w() { return controller2w_; }

  // Optional, YAML-provided 2W controller identity (see iohc/__init__.py's
  // controller_address/system_key config comment) - mirrors
  // IOHCCover::set_fixed_node()/set_fixed_key()'s exact rationale: leave
  // unset to keep the default random-generate-and-persist-on-device
  // behavior, or set both to survive a board replacement without losing
  // any already-bonded motor's relationship with this bridge.
  void set_fixed_controller_address(const std::string &hex) { fixed_controller_hex_ = hex; }
  void set_fixed_system_key(const std::string &hex) { fixed_system_key_hex_ = hex; }

  // Fast cooperative 3-channel RX hop (Finding 31/32) - DISABLED (CH2 only)
  // by default. Deliberately NOT built on iohcRadio's own ISR-driven
  // num_freqs/tickerCounter auto-hop mechanism (which stays permanently
  // disabled, num_freqs=1) - that's a genuine hardware interrupt and can
  // preempt mid-instruction, which would race against the
  // retune()-then-send() sequence every TX call site relies on. This hop
  // instead mirrors how github.com/laberning/home_io_control's own
  // reference implementation actually achieves its documented ~2.7ms hop
  // cadence: a cooperative maybe_hop()-style check with no interrupt
  // involved, so it can only ever run between top-level calls, never mid-TX
  // - no critical section needed, nothing to race.
  //
  // Why disabled by default (Finding 32, real-hardware regression): every
  // 1W frame (any remote) and the vast majority of ordinary 2W traffic
  // (TaHoma's own routine polling of already-bonded motors) is CH2-only in
  // practice - confirmed by this entire project's history of reliable
  // CH2-only decode, and by proto_timing.h's own "commands are sent on
  // CH2" statement. The reference hops continuously by default because
  // it's an ACTIVE controller: if a response lands on the wrong channel,
  // it can just retry its own command. This bridge's passive decode of
  // TaHoma's traffic has no such retry available - one missed frame is
  // just gone. Confirmed on real hardware: with the hop always on, a full
  // 7-minute capture caught every 2W exchange but zero 1W frames from a
  // physical remote pressed repeatedly during that window.
  //
  // Two independent callers can each request the hop - the dedicated
  // "Channel Hop (2W)" switch (somfy-io-bridge.yaml,
  // deliberately separate from Debug Logging - opportunistic background
  // sniffing is a distinct concern from log verbosity, and running it
  // unattended for a long stretch shouldn't require also paying for
  // continuous VERY_VERBOSE logging) and IOHCController2W::arm_bonding()
  // (the ~60s DISCOVER-wait window) - tracked separately so one's request
  // ending doesn't kill the other's still-active one.
  void set_manual_hop_wanted(bool wanted);
  void set_bonding_hop_wanted(bool wanted);

 protected:
  void maybe_hop_();
  void hop_wanted_changed_();
  uint32_t last_hop_us_{0};
  uint8_t hop_channel_idx_{0};
  bool manual_hop_wanted_{false};
  bool bonding_hop_wanted_{false};
  std::string fixed_controller_hex_;
  std::string fixed_system_key_hex_;

  IOHC::IOHCController2W controller2w_;
  // Total received frame count, any source - only used for the debug log
  // line in on_receive() (frame numbering), no HA-facing consumer anymore
  // (removed from the OLED status page in favor of per-motor RSSI on each
  // cover's own page).
  uint32_t packets_received_{0};

  // Per-source-address RSSI for EVERY address ever heard, logged only (not
  // exposed as HA entities) - most senders are foreign/unregistered (other
  // remotes, TaHoma itself), not known ahead of time, so a fixed sensor per
  // entry here would be unbounded and mostly meaningless. Packed as
  // (source[0]<<16 | source[1]<<8 | source[2]) - see on_receive(). For the
  // specific subset of addresses that ARE known ahead of time (a cover's own
  // configured motor_address), see position_covers_ below and
  // IOHCCover::update_last_rssi() - that's the one exposed as a real sensor.
  std::unordered_map<uint32_t, float> address_rssi_;

  // Motor address (packed the same way) -> the cover to notify when a real
  // position for that motor is passively decoded. Only covers with
  // motor_address set are registered here.
  std::unordered_map<uint32_t, IOHCCover *> position_covers_;
};

}  // namespace iohc
}  // namespace esphome
