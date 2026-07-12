#include "iohc.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "cover/iohc_cover.h"

namespace esphome {
namespace iohc {

static const char *const TAG = "iohc";

// MAX_FREQS stays 1 here (iohcRadio's own ISR-driven auto-hop stays
// permanently disabled, see iohc.h's maybe_hop_() comment) - CH2 is still
// the single frequency iohcRadio itself is "started" on; the 3-channel hop
// is layered on top cooperatively via maybe_hop_() below.
static uint32_t scan_freqs[1] = {CHANNEL2};

// Fast cooperative hop cadence (Finding 31/32) - matches
// home_io_control/proto_timing.h's own documented ~2.7ms/3-channel idle
// hop (roughly 900us/channel), only while something has explicitly
// requested it (see iohc.h's set_manual_hop_wanted()/set_bonding_hop_wanted()).
// Real achieved rate depends on how fast ESPHome's own loop() cycles on
// this hardware - this is the target, not a guarantee, since there's no
// hardware timer backing it (deliberately, see iohc.h).
static constexpr uint32_t HOP_INTERVAL_US = 900;
static constexpr uint32_t HOP_CHANNELS[3] = {CHANNEL2, CHANNEL1, CHANNEL3};

void IOHCComponent::setup() {
  ESP_LOGCONFIG(TAG, "Starting IOHC radio (vendored rspaargaren/iohomecontrol stack)...");
  IOHC::IohcPacketDelegate rx_cb(&IOHCComponent::on_receive, this);
  IOHC::iohcRadio::getInstance()->start(1, scan_freqs, 0, rx_cb, nullptr);
  controller2w_.begin(IOHC::iohcRadio::getInstance(), this, fixed_controller_hex_, fixed_system_key_hex_);
}

void IOHCComponent::loop() {
  // Bonding-attempt timeouts and per-command timeouts - see
  // iohc_controller2w.cpp's loop().
  controller2w_.loop();
  maybe_hop_();
}

void IOHCComponent::maybe_hop_() {
  // Disabled (CH2 only) unless something explicitly wants it - see iohc.h's
  // comment on why this isn't the default (Finding 32).
  if (!manual_hop_wanted_ && !bonding_hop_wanted_)
    return;
  // Never yank the radio off-frequency mid-reception, and never hop while
  // a TX is in flight (belt-and-suspenders - see iohc.h's comment on why
  // there's no actual race here, this is just cheap extra safety).
  auto state = IOHC::iohcRadio::radioState;
  if (state == IOHC::iohcRadio::RadioState::PREAMBLE || state == IOHC::iohcRadio::RadioState::PAYLOAD ||
      state == IOHC::iohcRadio::RadioState::TX) {
    return;
  }
  uint32_t now = esphome::micros();
  if ((now - last_hop_us_) < HOP_INTERVAL_US)
    return;
  last_hop_us_ = now;
  hop_channel_idx_ = (hop_channel_idx_ + 1) % 3;
  IOHC::iohcRadio::getInstance()->retune(HOP_CHANNELS[hop_channel_idx_]);
}

void IOHCComponent::hop_wanted_changed_() {
  if (!manual_hop_wanted_ && !bonding_hop_wanted_) {
    // Nobody wants the hop anymore - go back to CH2 immediately rather
    // than waiting for the next (now-disabled) hop tick to happen to land
    // there.
    IOHC::iohcRadio::getInstance()->retune(CHANNEL2);
  }
}

void IOHCComponent::set_manual_hop_wanted(bool wanted) {
  manual_hop_wanted_ = wanted;
  hop_wanted_changed_();
}

void IOHCComponent::set_bonding_hop_wanted(bool wanted) {
  bonding_hop_wanted_ = wanted;
  hop_wanted_changed_();
}

void IOHCComponent::register_cover_for_position_updates(const IOHC::address &motor_address, IOHCCover *cover) {
  uint32_t packed = (static_cast<uint32_t>(motor_address[0]) << 16) | (static_cast<uint32_t>(motor_address[1]) << 8) |
                     static_cast<uint32_t>(motor_address[2]);
  this->position_covers_[packed] = cover;
}

bool IOHCComponent::on_receive(IOHC::iohcPacket *packet) {
  this->packets_received_++;

  uint32_t address = (static_cast<uint32_t>(packet->payload.packet.header.source[0]) << 16) |
                      (static_cast<uint32_t>(packet->payload.packet.header.source[1]) << 8) |
                      static_cast<uint32_t>(packet->payload.packet.header.source[2]);
  this->address_rssi_[address] = packet->rssi;
  ESP_LOGI(TAG, "Address %06X last rssi: %.1f dBm", address, packet->rssi);

  // Our own bridge's view of this motor's signal strength - distinct from
  // Overkiz's "RSSI Level" sensor, which reflects TaHoma's own radio, not
  // ours (see README's "Real position feedback" section for the same
  // distinction applied to position data). Any frame with a matching source
  // address counts, 1W or 2W, decoded content or not - RSSI is a property
  // of the reception itself, not something a garbled payload could corrupt.
  if (address != 0) {
    auto rssi_it = this->position_covers_.find(address);
    if (rssi_it != this->position_covers_.end()) {
      rssi_it->second->update_last_rssi(packet->rssi);
    }
  }

  ESP_LOGI(TAG, "Frame #%u received, length=%u, rssi=%.1f dBm", this->packets_received_, packet->buffer_length,
           packet->rssi);

  // This bridge's own 2W bonding (Phase 3) - only ever consumes a frame
  // while a bonding attempt is armed AND the frame's source matches that
  // attempt's specific target motor (see IOHCController2W::handle_frame()),
  // so this is a cheap no-op in the common case and never interferes with
  // the passive-decode path below, which stays completely unmodified.
  controller2w_.handle_frame(packet);

  // Passive 2W position sync - see register_cover_for_position_updates()'s
  // comment and README's "Real position feedback" section. Only 2W (Protocol
  // bit clear) CMD 0x04 "Private Command Answer" frames from a tracked motor
  // address carry this: bytes[2-3] of the answer payload are the same Main
  // Parameter value used by the CMD 0x00 movement command itself, on the
  // documented "0x0000-0xC800 = 0-100%" scale (closure %, 0=open/100=closed -
  // the OPPOSITE of ESPHome's own 0=closed/1=open convention, hence the
  // 1.0-x conversion below). Requires buffer_length >= 13 to safely read
  // bytes[2-3] (buffer[11]/buffer[12]).
  //
  // `address != 0` rejects obvious garbage (a frame decoded as FROM 000000)
  // but is only a partial mitigation - no CRC/integrity check exists
  // anywhere in the RX pipeline, so a garbled frame with a real registered
  // address and plausible length would still get through. This callback
  // never writes into the cover's own position/state for that reason - see
  // IOHCCover::update_real_position().
  if (!packet->payload.packet.header.CtrlByte1.asStruct.Protocol && packet->payload.packet.header.cmd == 0x04 &&
      packet->buffer_length >= 13 && packet->buffer_length <= MAX_FRAME_LEN && address != 0) {
    auto it = this->position_covers_.find(address);
    if (it != this->position_covers_.end()) {
      uint16_t raw = (static_cast<uint16_t>(packet->payload.buffer[11]) << 8) | packet->payload.buffer[12];
      // Only 0x0000-0xC800 is a real closure percentage. This field echoes
      // whatever Main Parameter the LAST command used - if that command was
      // Stop (0xD200), Vent/Favorite (0xD800), Secured target (0xD100), or
      // Default (0xD300), raw lands above 0xC800 and is not a position at
      // all. Previously this was blindly divided and clamped to 0-100,
      // silently turning a Stop-echo (0xD200 / 512 = 105%) into a
      // plausible-looking but wrong 100% - caught 2026-07-12 when a
      // passively-decoded "100%" didn't match Overkiz's real 29% for the
      // same motor. Skip the update entirely rather than guess.
      if (raw > 0xC800) {
        ESP_LOGD(TAG, "Passive 2W decode: ignoring non-position Main echo 0x%04X (last command wasn't a move)", raw);
      } else {
        float closure_percent = raw / 512.0f;  // 0xC800 (51200) == 100%
        it->second->update_real_position(closure_percent);
      }
    }
  }

  return true;
}

void IOHCComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "IOHC (Somfy IO / io-homecontrol) radio:");
  ESP_LOGCONFIG(TAG, "  Phase 0/1: vendored radio stack, single fixed channel (868.95MHz), RX only");
}

}  // namespace iohc
}  // namespace esphome
