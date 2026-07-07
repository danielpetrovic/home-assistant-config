#include "iohc.h"
#include "esphome/core/log.h"

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
  return true;
}

void IOHCComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "IOHC (Somfy IO / io-homecontrol) radio:");
  ESP_LOGCONFIG(TAG, "  Phase 0/1: vendored radio stack, single fixed channel (868.95MHz), RX only");
}

}  // namespace iohc
}  // namespace esphome
