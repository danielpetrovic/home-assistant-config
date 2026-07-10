/*
   Copyright (c) 2024. CRIDP https://github.com/cridp

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

           http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include "iohc_controller2w.h"
#include "iohcCryptoHelpers.h"
#include "cover/iohc_cover.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <esp_system.h>
#include <cstring>

namespace IOHC {

    static const char *const TAG = "iohc.controller2w";

    // Timeouts. 60s for the initial DISCOVER wait matches the real,
    // user-facing "go press the motor's pairing button now" window (same
    // shape as the existing 1W Program-button UX); the rest are generous
    // relative to the ~1-2s response times seen across every real 2W
    // exchange captured this session (io-2w-protocol.md Findings 5-14).
    static constexpr uint32_t TIMEOUT_ARMED_MS = 60000;
    static constexpr uint32_t TIMEOUT_STEP_MS = 5000;
    static constexpr uint32_t TIMEOUT_CMD_MS = 10000;

    uint32_t IOHCController2W::pack_address(const IOHC::address &addr) {
        return (static_cast<uint32_t>(addr[0]) << 16) | (static_cast<uint32_t>(addr[1]) << 8) |
               static_cast<uint32_t>(addr[2]);
    }

    void IOHCController2W::begin(iohcRadio *radio, const std::string &fixed_controller_hex) {
        radio_ = radio;
        load_or_generate_identity(fixed_controller_hex);
    }

    void IOHCController2W::load_or_generate_identity(const std::string &fixed_controller_hex) {
        // Fixed namespace, not hashed - there is only ever one controller
        // identity for the whole bridge (shared across every cover's 2W
        // bonding), unlike IOHCRemote1W's per-cover "io"+hash namespaces.
        identity_prefs_.begin("iohc2wgw", false);

        if (!fixed_controller_hex.empty()) {
            hexStringToBytes(fixed_controller_hex, controller_address_);
            ESP_LOGCONFIG(TAG, "Using fixed 2W controller identity %02X%02X%02X from YAML",
                          controller_address_[0], controller_address_[1], controller_address_[2]);
            return;
        }

        if (identity_prefs_.isKey("addr") && identity_prefs_.getBytesLength("addr") == sizeof(controller_address_)) {
            identity_prefs_.getBytes("addr", controller_address_, sizeof(controller_address_));
            ESP_LOGCONFIG(TAG, "Loaded 2W controller identity %02X%02X%02X", controller_address_[0],
                          controller_address_[1], controller_address_[2]);
            return;
        }

        for (uint8_t &b : controller_address_) b = esp_random() & 0xff;
        identity_prefs_.putBytes("addr", controller_address_, sizeof(controller_address_));
        ESP_LOGCONFIG(TAG, "Generated new 2W controller identity %02X%02X%02X", controller_address_[0],
                      controller_address_[1], controller_address_[2]);
    }

    void IOHCController2W::arm_bonding(const IOHC::address &motor_address, esphome::iohc::IOHCCover *cover) {
        static const IOHC::address zero{0, 0, 0};
        if (memcmp(motor_address, zero, sizeof(IOHC::address)) == 0) {
            ESP_LOGW(TAG, "Program (2W) pressed but this cover has no motor_address configured - refusing to arm "
                          "(see README's Real position feedback section for how to look it up). Will not listen "
                          "for just any DISCOVER broadcast.");
            return;
        }
        if (attempt_.state != BondState::IDLE) {
            ESP_LOGW(TAG, "Program (2W) already in progress for %02X%02X%02X - ignoring new request until it finishes "
                          "or times out",
                     attempt_.target_motor[0], attempt_.target_motor[1], attempt_.target_motor[2]);
            return;
        }
        memcpy(attempt_.target_motor, motor_address, sizeof(IOHC::address));
        attempt_.armed_cover = cover;
        attempt_.state = BondState::ARMED_WAITING_DISCOVER;
        attempt_.state_entered_ms = esphome::millis();
        ESP_LOGI(TAG, "Armed for 2W bonding with %02X%02X%02X - press the motor's own physical pairing button "
                      "within 60s",
                 motor_address[0], motor_address[1], motor_address[2]);
    }

    void IOHCController2W::reset_bonding(BondState to_state, const char *reason) {
        ESP_LOGW(TAG, "2W bonding with %02X%02X%02X: %s", attempt_.target_motor[0], attempt_.target_motor[1],
                 attempt_.target_motor[2], reason);
        attempt_.state = to_state;
        attempt_.armed_cover = nullptr;
        if (to_state != BondState::BONDED) {
            // Explicitly does not touch sessions_ for any OTHER motor -
            // see the plan file's "never let one motor's bonding attempt
            // touch another's persisted state" principle.
            attempt_.state = BondState::IDLE;
        }
    }

    void IOHCController2W::loop() {
        if (attempt_.state == BondState::IDLE || attempt_.state == BondState::BONDED) return;
        uint32_t elapsed = esphome::millis() - attempt_.state_entered_ms;
        uint32_t timeout = (attempt_.state == BondState::ARMED_WAITING_DISCOVER) ? TIMEOUT_ARMED_MS : TIMEOUT_STEP_MS;
        if (elapsed > timeout) {
            reset_bonding(BondState::FAILED, "timed out, resetting - press Program (2W) again to retry");
        }

        // In-flight per-command timeouts, independent of the bonding FSM
        // above - see Session2W::cmd_state.
        for (auto &kv : sessions_) {
            Session2W &s = kv.second;
            if (s.cmd_state != CmdState::IDLE && (esphome::millis() - s.cmd_sent_ms) > TIMEOUT_CMD_MS) {
                ESP_LOGW(TAG, "2W command to a bonded motor timed out waiting for challenge/answer - giving up "
                              "on this attempt");
                s.cmd_state = CmdState::IDLE;
            }
        }
    }

    bool IOHCController2W::is_bonded(const IOHC::address &motor_address) const {
        auto it = sessions_.find(pack_address(motor_address));
        return it != sessions_.end() && it->second.bonded;
    }

    // --- Frame builders ---
    // Header conventions (MsgLen = buffer_length-1, Protocol=0 for 2W,
    // CtrlByte2=0) mirror IOHCRemote1W::forge_packet()'s pattern, adapted
    // for 2W's unicast (not broadcast-group) addressing. CtrlByte2=0 is
    // inferred from every real 2W capture this session never showing any
    // of decode()'s [LPM]/[B]/[R]/[PRIO] tags for ordinary command frames -
    // not independently confirmed for frames WE originate, since we have
    // no real transmitted-by-us 2W capture to check against yet.

    static void forge_2w_packet(iohcPacket *packet, const IOHC::address &from, const IOHC::address &to) {
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen = sizeof(_header) - 1;
        packet->payload.packet.header.CtrlByte1.asStruct.Protocol = 0;  // 2W
        packet->payload.packet.header.CtrlByte1.asStruct.StartFrame = 1;
        packet->payload.packet.header.CtrlByte1.asStruct.EndFrame = 1;
        packet->payload.packet.header.CtrlByte2.asByte = 0;
        memcpy(packet->payload.packet.header.source, from, sizeof(IOHC::address));
        memcpy(packet->payload.packet.header.target, to, sizeof(IOHC::address));
        packet->frequency = CHANNEL2;
        packet->repeatTime = 0;
        packet->repeat = 0;  // 2W is single-shot per direction, unlike 1W's repeat=4 broadcast redundancy - see Finding 13/plan file, deliberate, do not "fix" back to 1W's convention
        packet->lock = false;
    }

    void IOHCController2W::send_discover_answer(const IOHC::address &to) {
        // UNVERIFIED - see this file's header comment. Payload shape from
        // upstream's own RECEIVED_DISCOVER_0x28 handler example:
        // {0xff,0xc0,gw0,gw1,gw2,manufacturer,info,0x00,0x00} - matches
        // iohcPacket.h's _p0x29_ack struct.
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x29_ack);
        packet->payload.packet.header.cmd = 0x29;
        packet->payload.packet.msg.p0x29_ack.cap1 = 0xff;
        packet->payload.packet.msg.p0x29_ack.cap2 = 0xc0;
        memcpy(packet->payload.packet.msg.p0x29_ack.gateway, controller_address_, sizeof(IOHC::address));
        packet->payload.packet.msg.p0x29_ack.manufacturer = 0x0b;  // OverKiz, per upstream's own example
        packet->payload.packet.msg.p0x29_ack.info = 0x00;
        packet->payload.packet.msg.p0x29_ack.reserved[0] = 0x00;
        packet->payload.packet.msg.p0x29_ack.reserved[1] = 0x00;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent DISCOVER_ANSWER (0x29) to %02X%02X%02X", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_discover_actuator(const IOHC::address &to) {
        // 0-byte payload, confirmed shape (Finding 14's real roll-call
        // capture) - only the trigger/sequencing context here is
        // unverified, not this frame's own bytes.
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.cmd = 0x2C;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent DISCOVER_ACTUATOR (0x2C) to %02X%02X%02X", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_key_transfert(const IOHC::address &to, const uint8_t *challenge6) {
        // UNVERIFIED - see this file's header comment. Algorithm from
        // iohcCrypto::build_key_transfert_response() (Phase 3b) - itself
        // unverified against a real KEY_TRANSFERT capture, since none
        // exists yet (Finding 14).
        uint8_t response[16];
        iohcCrypto::build_key_transfert_response(challenge6, response);

        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x32);
        packet->payload.packet.header.cmd = 0x32;
        memcpy(packet->payload.packet.msg.p0x32.encrypted_key, response, 16);
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent KEY_TRANSFERT (0x32) to %02X%02X%02X", to[0], to[1], to[2]);
    }

    bool IOHCController2W::handle_frame(IOHC::iohcPacket *packet) {
        // Only ever consume frames while a bonding attempt is in progress,
        // and only from the exact motor that attempt is scoped to - never
        // touch anything else, so this never interferes with the existing,
        // unrelated passive-decode Tier 1 feature (iohc.cpp's on_receive()
        // still gets first look at everything, this is called after/instead
        // only for the bonding-family commands it explicitly routes here).
        if (attempt_.state == BondState::IDLE || attempt_.state == BondState::BONDED) return false;

        const auto &source = packet->payload.packet.header.source;
        if (memcmp(source, attempt_.target_motor, sizeof(IOHC::address)) != 0) return false;

        uint8_t cmd = packet->payload.packet.header.cmd;

        switch (attempt_.state) {
            case BondState::ARMED_WAITING_DISCOVER:
                if (cmd != 0x28) return false;
                ESP_LOGI(TAG, "Received DISCOVER (0x28) from %02X%02X%02X - responding",
                         source[0], source[1], source[2]);
                send_discover_answer(source);
                attempt_.state = BondState::SENT_DISCOVER_ANSWER;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::SENT_DISCOVER_ANSWER:
                // Genuinely unconfirmed next step - see this file's header
                // comment and the plan file's "0x29-direction" open
                // question. Best current guess: proceed straight to
                // DISCOVER_ACTUATOR, matching the shape of the box-driven
                // roll-call sequence confirmed in Finding 14, rather than
                // upstream's own confusing RECEIVED_DISCOVER_ANSWER_0x29
                // handler (which appears to expect an answer to something
                // we already answered, on a straight code read).
                send_discover_actuator(source);
                attempt_.state = BondState::SENT_DISCOVER_ACTUATOR;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::SENT_DISCOVER_ACTUATOR:
                if (cmd != 0x2D) return false;
                ESP_LOGI(TAG, "Received DISCOVER_ACTUATOR_ACK (0x2D) - waiting for LAUNCH_KEY_TRANSFERT");
                attempt_.state = BondState::WAITING_LAUNCH_KEY_TRANSFERT;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::WAITING_LAUNCH_KEY_TRANSFERT:
                if (cmd != 0x38 || packet->buffer_length - 9 != 6) return false;
                ESP_LOGI(TAG, "Received LAUNCH_KEY_TRANSFERT (0x38) - sending KEY_TRANSFERT");
                send_key_transfert(source, packet->payload.packet.msg.p0x38.challenge);
                attempt_.state = BondState::SENT_KEY_TRANSFERT;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::SENT_KEY_TRANSFERT: {
                // Confirmation via either a 0x3C/0x3D round trip or a
                // direct 0x33 KEY_TRANSFERT_ACK - both plausible per
                // upstream's command enum, neither confirmed for this
                // specific step. Accept either as success.
                if (cmd == 0x33 || cmd == 0x3D) {
                    ESP_LOGI(TAG, "2W bonding with %02X%02X%02X succeeded", source[0], source[1], source[2]);
                    Session2W &s = sessions_[pack_address(attempt_.target_motor)];
                    s.bonded = true;
                    // key[] deliberately left zeroed - whether a real
                    // per-motor secret exists at all is still unresolved
                    // (see the addendum to Finding 6, and Finding 14's
                    // second confirmation) - nothing to store yet.
                    attempt_.state = BondState::BONDED;
                    attempt_.armed_cover = nullptr;
                    return true;
                }
                return false;
            }

            default:
                return false;
        }
    }
}
