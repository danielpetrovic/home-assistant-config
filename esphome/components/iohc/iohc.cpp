#include "iohc.h"
#include "esphome/core/log.h"
#include "cover/iohc_cover.h"

namespace esphome {
namespace iohc {

static const char *const TAG = "iohc";

// MAX_FREQS=1 in iohc_board_config.h - single fixed channel for this phase.
static uint32_t scan_freqs[1] = {CHANNEL2};

void IOHCComponent::setup() {
  ESP_LOGCONFIG(TAG, "Starting IOHC radio (vendored rspaargaren/iohomecontrol stack)...");
  IOHC::IohcPacketDelegate rx_cb(&IOHCComponent::on_receive, this);
  IOHC::iohcRadio::getInstance()->start(1, scan_freqs, 0, rx_cb, nullptr);
}

void IOHCComponent::register_cover_for_position_updates(const IOHC::address &motor_address, IOHCCover *cover) {
  uint32_t packed = (static_cast<uint32_t>(motor_address[0]) << 16) | (static_cast<uint32_t>(motor_address[1]) << 8) |
                     static_cast<uint32_t>(motor_address[2]);
  this->position_covers_[packed] = cover;
}

bool IOHCComponent::on_receive(IOHC::iohcPacket *packet) {
  this->packets_received_++;
  this->last_rssi_ = packet->rssi;

  uint32_t address = (static_cast<uint32_t>(packet->payload.packet.header.source[0]) << 16) |
                      (static_cast<uint32_t>(packet->payload.packet.header.source[1]) << 8) |
                      static_cast<uint32_t>(packet->payload.packet.header.source[2]);
  this->address_rssi_[address] = packet->rssi;
  ESP_LOGI(TAG, "Address %06X last rssi: %.1f dBm", address, packet->rssi);

  ESP_LOGI(TAG, "Frame #%u received, length=%u, rssi=%.1f dBm", this->packets_received_, packet->buffer_length,
           packet->rssi);

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
      float closure_percent = raw / 512.0f;  // 0xC800 (51200) == 100%
      it->second->update_real_position(closure_percent);
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
