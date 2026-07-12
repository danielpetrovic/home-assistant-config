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
#include "iohc.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <esp_system.h>
#include <cstring>

namespace IOHC {

    static const char *const TAG = "iohc.controller2w";

    // Timeouts. 60s overall for discovery matches the real, user-facing "go
    // press the motor's pairing button now" window (same shape as the
    // existing 1W Program-button UX). The rest are generous relative to the
    // ~1-2s response times seen across every real 2W exchange captured this
    // session (io-2w-protocol.md Findings 5-14).
    static constexpr uint32_t TIMEOUT_ARMED_MS = 60000;
    static constexpr uint32_t TIMEOUT_STEP_MS = 5000;
    static constexpr uint32_t TIMEOUT_CMD_MS = 10000;

    // DISCOVER-phase resend timing (Finding 22). RX coverage across
    // CH1/CH2/CH3 is now the bridge-wide cooperative hop's job (Finding 31,
    // esphome::iohc::IOHCComponent::maybe_hop_()) - this class only manages
    // when to resend the 0x28 itself, always on CH2. DISCOVER_RESEND_MS
    // matches home_io_control/proto_timing.h's own
    // PAIRING_DISCOVERY_WAIT_MS default (2000ms) reasonably closely.
    static constexpr uint32_t DISCOVER_RESEND_MS = 2000;

    // Fixed SET_CONFIG1 payload - asks the device to auto-broadcast status
    // updates. Matches github.com/laberning/home_io_control's own working
    // controller captures (Finding 20). Best-effort only, not required for
    // BONDED status.
    static constexpr uint8_t SET_CONFIG1_PAYLOAD[5] = {0xE0, 0x10, 0x0A, 0x08, 0x00};

    // Listen-Before-Talk (Finding 22) - matches home_io_control's
    // proto_timing.h ETSI EN 300 220 constants exactly (-90dBm threshold,
    // 5 retries, 5ms backoff). This bridge never had any LBT before this -
    // a real gap given this session directly captured genuine RF
    // contention (real TaHoma<->motor traffic landing mid-attempt during a
    // bonding test).
    static constexpr int16_t LBT_RSSI_THRESHOLD_DBM = -90;
    static constexpr uint8_t LBT_MAX_RETRIES = 5;
    static constexpr uint32_t LBT_RETRY_DELAY_MS = 5;

    uint32_t IOHCController2W::pack_address(const IOHC::address &addr) {
        return (static_cast<uint32_t>(addr[0]) << 16) | (static_cast<uint32_t>(addr[1]) << 8) |
               static_cast<uint32_t>(addr[2]);
    }

    void IOHCController2W::begin(iohcRadio *radio, esphome::iohc::IOHCComponent *owner,
                                  const std::string &fixed_controller_hex, const std::string &fixed_system_key_hex) {
        radio_ = radio;
        owner_ = owner;
        load_or_generate_identity(fixed_controller_hex);
        load_or_generate_system_key(fixed_system_key_hex);
    }

    void IOHCController2W::load_or_generate_system_key(const std::string &fixed_system_key_hex) {
        // Same "iohc2wgw" namespace as the controller address - one shared
        // controller identity, one shared secret, both persisted together.
        if (!fixed_system_key_hex.empty()) {
            hexStringToBytes(fixed_system_key_hex, system_key_);
            ESP_LOGCONFIG(TAG, "Using fixed 2W system key from YAML");
            return;
        }

        if (identity_prefs_.isKey("syskey") && identity_prefs_.getBytesLength("syskey") == sizeof(system_key_)) {
            identity_prefs_.getBytes("syskey", system_key_, sizeof(system_key_));
            ESP_LOGCONFIG(TAG, "Loaded 2W system key");
            return;
        }

        for (uint8_t &b : system_key_) b = esp_random() & 0xff;
        identity_prefs_.putBytes("syskey", system_key_, sizeof(system_key_));
        ESP_LOGCONFIG(TAG, "Generated new 2W system key");
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
        attempt_.state = BondState::WAITING_DISCOVER_RESP;
        attempt_.state_entered_ms = esphome::millis();
        attempt_.discover_retries = 0;
        if (owner_ != nullptr)
            owner_->set_bonding_hop_wanted(true);
        send_discover();
        attempt_.last_discover_sent_ms = esphome::millis();
        ESP_LOGI(TAG, "2W bonding with %02X%02X%02X: sent DISCOVER (0x28) on CH2, waiting up to 60s (resending "
                      "every %us) - RX coverage across CH1/CH2/CH3 is enabled for the duration of this attempt - "
                      "make sure the motor's pairing window is already open (hold PROG on an existing paired "
                      "remote first, same as before)",
                 motor_address[0], motor_address[1], motor_address[2], DISCOVER_RESEND_MS / 1000);
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
        // Release this attempt's own hop request (Finding 32) - if Debug
        // Logging is still separately requesting it, the hop stays active;
        // otherwise this drops back to CH2-only immediately.
        if (owner_ != nullptr)
            owner_->set_bonding_hop_wanted(false);
    }

    void IOHCController2W::finalize_bonding(const IOHC::address &motor) {
        Session2W &s = sessions_[pack_address(motor)];
        s.bonded = true;
        memcpy(s.key, system_key_, sizeof(system_key_));
        ESP_LOGI(TAG, "2W bonding with %02X%02X%02X succeeded - system key now shared", motor[0], motor[1],
                 motor[2]);
        send_set_config1(motor);
        // Naming is deliberately not part of the protocol-level bonding
        // sequence (confirmed via laberning/home_io_control's own
        // pairing_engine.cpp - discover_and_pair() doesn't touch it
        // either), but setting it here is a harmless, useful default so
        // the motor's real name matches its ESPHome config. GET_NAME
        // (send_get_name()) stays a separate, opt-in diagnostic action.
        if (attempt_.armed_cover != nullptr)
            send_set_name(motor, attempt_.armed_cover->get_name().c_str());
        attempt_.state = BondState::BONDED;
        attempt_.armed_cover = nullptr;
        if (owner_ != nullptr)
            owner_->set_bonding_hop_wanted(false);
    }

    void IOHCController2W::loop() {
        // In-flight per-command timeouts, independent of the bonding FSM
        // below - see Session2W::cmd_state. Deliberately placed BEFORE the
        // attempt_.state early-return below: this ran only while a bonding
        // attempt happened to be active until this fix (2026-07-11), since
        // attempt_.state is IDLE almost all the time (no bonding attempt in
        // progress is the normal resting state) - meaning a command's
        // cmd_state could get stuck at SENT_WAITING_CHALLENGE/
        // ANSWERED_WAITING_ACK forever, permanently refusing every further
        // command to that motor until a reboot. Confirmed on real hardware:
        // a Force Test Bond (2W) Stop command that never got a 0x3C answer
        // never timed out and blocked all further commands to that motor.
        for (auto &kv : sessions_) {
            Session2W &s = kv.second;
            if (s.cmd_state != CmdState::IDLE && (esphome::millis() - s.cmd_sent_ms) > TIMEOUT_CMD_MS) {
                ESP_LOGW(TAG, "2W command to a bonded motor timed out waiting for challenge/answer - giving up "
                              "on this attempt");
                s.cmd_state = CmdState::IDLE;
            }
        }

        // Key sniffing (Finding 31) no longer needs any loop()-driven
        // hop/timeout management - it's a pure passive watcher inside
        // handle_frame() now, same as the ordinary Target Closure decode.
        // RX coverage across all 3 channels comes from the bridge-wide
        // cooperative hop (esphome::iohc::IOHCComponent::maybe_hop_()).

        if (attempt_.state == BondState::IDLE || attempt_.state == BondState::BONDED) return;

        // WAITING_DISCOVER_RESP: resend 0x28 (always on CH2, via
        // send_discover() which retunes itself) every DISCOVER_RESEND_MS -
        // RX coverage across CH1/CH2/CH3 in between comes from the
        // bridge-wide cooperative hop, not anything managed here. A single
        // 0x28 could also easily be sent before the user's paired remote
        // has actually opened the motor's pairing window, hence the resend.
        if (attempt_.state == BondState::WAITING_DISCOVER_RESP) {
            uint32_t now = esphome::millis();
            if ((now - attempt_.last_discover_sent_ms) > DISCOVER_RESEND_MS) {
                attempt_.discover_retries++;
                send_discover();
                attempt_.last_discover_sent_ms = esphome::millis();
                ESP_LOGD(TAG, "2W bonding: resent DISCOVER (0x28) on CH2, attempt %u", attempt_.discover_retries + 1);
            }
        }

        uint32_t elapsed = esphome::millis() - attempt_.state_entered_ms;
        uint32_t timeout = (attempt_.state == BondState::WAITING_DISCOVER_RESP) ? TIMEOUT_ARMED_MS : TIMEOUT_STEP_MS;
        if (elapsed > timeout) {
            reset_bonding(BondState::FAILED, "timed out, resetting - press Program (2W) again to retry");
        }
    }

    bool IOHCController2W::is_bonded(const IOHC::address &motor_address) const {
        auto it = sessions_.find(pack_address(motor_address));
        return it != sessions_.end() && it->second.bonded;
    }

    void IOHCController2W::reset_sniff_state() {
        // Called after a successful capture so the state machine is ready
        // to catch another bonding sequence - no "disarm" concept anymore,
        // this watcher just always runs (Finding 31).
        sniff_.state = SniffState::IDLE;
    }

    void IOHCController2W::listen_before_talk() {
        for (uint8_t i = 0; i < LBT_MAX_RETRIES; i++) {
            float rssi = Radio::readRssi();
            if (rssi < LBT_RSSI_THRESHOLD_DBM) return;
            ESP_LOGD(TAG, "LBT: channel busy (RSSI %.1f dBm), retry %u/%u", rssi, i + 1, LBT_MAX_RETRIES);
            esphome::delay(LBT_RETRY_DELAY_MS);
        }
        // Transmit anyway after LBT_MAX_RETRIES - never block a
        // user-triggered action forever waiting for a clear channel.
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

    void IOHCController2W::send_discover() {
        // Controller-initiated (Finding 20) - broadcasts to 0x00003B
        // (BROADCAST_DISCOVER in home_io_control's proto_constants.h; also
        // independently the exact broadcast target TaHoma itself uses for
        // its own 0x2A DISCOVER_REMOTE per Finding 14, so this address is
        // corroborated from two independent sources even though the
        // command byte differs). 0-byte payload.
        static const IOHC::address kBroadcastDiscover = {0x00, 0x00, 0x3B};
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, kBroadcastDiscover);
        packet->payload.packet.header.cmd = 0x28;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        // Always CH2 for TX (Finding 22 - "Commands are sent on CH2";
        // RX-side hopping in loop() is what catches a reply on CH1/CH3).
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);
    }

    void IOHCController2W::send_discover_confirmation(const IOHC::address &to) {
        // Byte-verified against nicolas5000/io-rts-esp32's real
        // create_discovery_confirmation_request(): init_frame(frame, true,
        // true, false, true) - is_2w=true, start=true, END=FALSE,
        // LOW_POWER=TRUE. Deliberately different from forge_2w_packet()'s
        // usual EndFrame=1/LowPower=0 defaults, overridden explicitly below
        // to match exactly. 0-byte payload, unicast (the motor address just
        // learned from its 0x29 response).
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.EndFrame = 0;
        packet->payload.packet.header.CtrlByte2.asStruct.LPM = 1;
        packet->payload.packet.header.cmd = 0x2C;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent DISCOVER_CONFIRMATION (0x2C) to %02X%02X%02X", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_key_init(const IOHC::address &to) {
        // 0-byte payload, unicast to the motor address just learned from
        // its 0x29 response.
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.cmd = 0x31;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent KEY_INIT (0x31) to %02X%02X%02X", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_key_transfer(const IOHC::address &to, const uint8_t *challenge6) {
        // Confirmed algorithm (Finding 20): build_key_transfert_response()
        // now XORs against this controller's real system_key_, not the
        // fixed transfer_key - see iohcCryptoHelpers.h/.cpp for the fix.
        uint8_t response[16];
        iohcCrypto::build_key_transfert_response(challenge6, system_key_, response);

        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x32);
        packet->payload.packet.header.cmd = 0x32;
        memcpy(packet->payload.packet.msg.p0x32.encrypted_key, response, 16);
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent KEY_TRANSFER (0x32) to %02X%02X%02X", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_set_config1(const IOHC::address &to) {
        // Best-effort - not required for BONDED status, no response waited
        // for. Fixed payload confirmed from home_io_control's own working
        // controller captures (Finding 20).
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x6f);
        packet->payload.packet.header.cmd = 0x6F;
        memcpy(packet->payload.packet.msg.p0x6f.data, SET_CONFIG1_PAYLOAD, sizeof(SET_CONFIG1_PAYLOAD));
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent SET_CONFIG1 (0x6F) to %02X%02X%02X (best-effort)", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_command(const IOHC::address &to, uint8_t main0, uint8_t main1,
                                         esphome::iohc::IOHCCover *cover) {
        Session2W &s = sessions_[pack_address(to)];
        if (!s.bonded) {
            ESP_LOGW(TAG, "2W command to %02X%02X%02X refused - not bonded yet (Program (2W) first)", to[0], to[1],
                      to[2]);
            return;
        }
        if (s.cmd_state != CmdState::IDLE) {
            ESP_LOGW(TAG, "2W command to %02X%02X%02X still waiting on a previous command - ignoring", to[0], to[1],
                      to[2]);
            return;
        }

        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x00_2w);
        packet->payload.packet.header.cmd = 0x00;
        packet->payload.packet.msg.p0x00_2w.origin = 0x01;
        setAcei(packet->payload.packet.msg.p0x00_2w.acei, 0xE7);
        packet->payload.packet.msg.p0x00_2w.main[0] = main0;
        packet->payload.packet.msg.p0x00_2w.main[1] = main1;
        packet->payload.packet.msg.p0x00_2w.reserved[0] = 0x00;
        packet->payload.packet.msg.p0x00_2w.reserved[1] = 0x00;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);

        s.last_sent_cmd = 0x00;
        s.last_sent_data = {0x01, 0xE7, main0, main1, 0x00, 0x00};
        s.cmd_state = CmdState::SENT_WAITING_CHALLENGE;
        s.cmd_sent_ms = esphome::millis();
        s.pending_cmd_cover = cover;
        ESP_LOGI(TAG, "Sent 2W command (main=%02X%02X) to %02X%02X%02X - waiting for challenge/response", main0,
                 main1, to[0], to[1], to[2]);
    }

    void IOHCController2W::send_get_name(const IOHC::address &to) {
        Session2W &s = sessions_[pack_address(to)];
        if (!s.bonded) {
            ESP_LOGW(TAG, "GET_NAME to %02X%02X%02X refused - not bonded yet (Program (2W) first)", to[0], to[1],
                     to[2]);
            return;
        }
        if (s.cmd_state != CmdState::IDLE) {
            ESP_LOGW(TAG, "GET_NAME to %02X%02X%02X still waiting on a previous command - ignoring", to[0], to[1],
                     to[2]);
            return;
        }

        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.cmd = 0x50;
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);

        s.last_sent_cmd = 0x50;
        s.last_sent_data = {};
        s.cmd_state = CmdState::SENT_WAITING_CHALLENGE;
        s.cmd_sent_ms = esphome::millis();
        s.pending_cmd_cover = nullptr;
        ESP_LOGI(TAG, "Sent GET_NAME (0x50) to %02X%02X%02X - waiting for challenge/response", to[0], to[1], to[2]);
    }

    void IOHCController2W::send_set_name(const IOHC::address &to, const std::string &name) {
        auto *packet = new iohcPacket;
        forge_2w_packet(packet, controller_address_, to);
        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x52);
        packet->payload.packet.header.cmd = 0x52;
        memset(packet->payload.packet.msg.p0x52.name, 0, sizeof(_p0x52::name));
        strncpy(packet->payload.packet.msg.p0x52.name, name.c_str(), sizeof(_p0x52::name) - 1);
        packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
        radio_->retune(CHANNEL2);
        listen_before_talk();
        radio_->send(packet);
        ESP_LOGI(TAG, "Sent SET_NAME (0x52) \"%s\" to %02X%02X%02X (best-effort)", name.c_str(), to[0], to[1], to[2]);
    }

    bool IOHCController2W::handle_frame(IOHC::iohcPacket *packet) {
        // Passive key-sniff (Findings 27/28) - observes only, never
        // consumes/returns true, so it can never interfere with anything
        // below or with iohc.cpp's own passive-decode path, both of which
        // must keep seeing every frame regardless of whether a sniff is
        // armed. Deliberately placed first/unconditional so it sees 0x31
        // even for a source/destination pair this bridge has no session or
        // bonding attempt for at all (the whole point - this watches OTHER
        // controllers' bonding exchanges, not this bridge's own). Always
        // runs (Finding 31) - no arm/disarm anymore.
        if (!packet->payload.packet.header.CtrlByte1.asStruct.Protocol) {
            const auto &src = packet->payload.packet.header.source;
            const auto &dst = packet->payload.packet.header.target;
            uint8_t cmd = packet->payload.packet.header.cmd;

            if (sniff_.state == SniffState::IDLE && cmd == 0x31) {
                memcpy(sniff_.box, src, sizeof(IOHC::address));
                memcpy(sniff_.device, dst, sizeof(IOHC::address));
                sniff_.state = SniffState::SAW_KEY_INIT;
                ESP_LOGI(TAG, "2W key sniff: saw KEY_INIT (0x31) %02X%02X%02X -> %02X%02X%02X, watching for its "
                              "CHALLENGE_REQUEST",
                         src[0], src[1], src[2], dst[0], dst[1], dst[2]);
            } else if (sniff_.state == SniffState::SAW_KEY_INIT && cmd == 0x3C &&
                       memcmp(src, sniff_.device, sizeof(IOHC::address)) == 0 &&
                       memcmp(dst, sniff_.box, sizeof(IOHC::address)) == 0) {
                memcpy(sniff_.challenge, packet->payload.packet.msg.p0x3c.challenge, sizeof(sniff_.challenge));
                sniff_.state = SniffState::SAW_CHALLENGE;
                ESP_LOGI(TAG, "2W key sniff: saw matching CHALLENGE_REQUEST (0x3C), watching for KEY_TRANSFER "
                              "(0x32)");
            } else if (sniff_.state == SniffState::SAW_CHALLENGE && cmd == 0x32 &&
                       memcmp(src, sniff_.box, sizeof(IOHC::address)) == 0 &&
                       memcmp(dst, sniff_.device, sizeof(IOHC::address)) == 0 &&
                       packet->buffer_length >= sizeof(_header) + sizeof(_p0x32)) {
                // Same primitive used to BUILD a real KEY_TRANSFER payload
                // (build_key_transfert_response()) - XOR is symmetric, so
                // calling it with the captured ciphertext in place of "our
                // own system_key" recovers the real system_key instead. See
                // that function's own doc comment (Finding 28).
                uint8_t derived_key[16];
                iohcCrypto::build_key_transfert_response(sniff_.challenge, packet->payload.packet.msg.p0x32.encrypted_key,
                                                          derived_key);
                std::string hex = bytesToHexString(derived_key, 16);
                ESP_LOGW(TAG, "2W key sniff SUCCESS: derived real system_key from %02X%02X%02X's bonding with "
                              "%02X%02X%02X: %s",
                         sniff_.box[0], sniff_.box[1], sniff_.box[2], sniff_.device[0], sniff_.device[1],
                         sniff_.device[2], hex.c_str());
                reset_sniff_state();
            }
        }

        // Per-command challenge/response for already-bonded motors (Phase
        // 3d) - independent of the bonding attempt_ FSM below, since this
        // fires for ordinary Open/Close/Position/Stop commands sent long
        // after bonding completed, not during an active bonding attempt.
        {
            const auto &src = packet->payload.packet.header.source;
            auto it = sessions_.find(pack_address(src));
            if (it != sessions_.end() && it->second.bonded && it->second.cmd_state != CmdState::IDLE) {
                Session2W &s = it->second;
                uint8_t cmd = packet->payload.packet.header.cmd;

                if (s.cmd_state == CmdState::SENT_WAITING_CHALLENGE && cmd == 0x3C) {
                    uint8_t answer[6];
                    iohcCrypto::build_challenge_answer(s.last_sent_cmd, s.last_sent_data,
                                                        packet->payload.packet.msg.p0x3c.challenge, s.key, answer);
                    auto *reply = new iohcPacket;
                    forge_2w_packet(reply, controller_address_, src);
                    reply->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x3d);
                    reply->payload.packet.header.cmd = 0x3D;
                    memcpy(reply->payload.packet.msg.p0x3d.response, answer, 6);
                    reply->buffer_length = reply->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
                    radio_->retune(CHANNEL2);
                    listen_before_talk();
                    radio_->send(reply);
                    s.cmd_state = CmdState::ANSWERED_WAITING_ACK;
                    s.cmd_sent_ms = esphome::millis();
                    ESP_LOGD(TAG, "Sent CHALLENGE_ANSWER (0x3D) for in-flight command to %02X%02X%02X", src[0],
                              src[1], src[2]);
                    return true;
                }

                if (s.cmd_state == CmdState::ANSWERED_WAITING_ACK && s.last_sent_cmd == 0x00 && cmd == 0x04 &&
                    packet->buffer_length >= 13 && packet->buffer_length <= MAX_FRAME_LEN) {
                    s.cmd_state = CmdState::IDLE;
                    uint16_t raw = (static_cast<uint16_t>(packet->payload.buffer[11]) << 8) |
                                   packet->payload.buffer[12];
                    esphome::iohc::IOHCCover *cover = s.pending_cmd_cover;
                    s.pending_cmd_cover = nullptr;
                    // Only 0x0000-0xC800 is a real closure percentage - a
                    // Stop command (main=0xD200) echoes back above that
                    // range and is not a position (same bug/fix as iohc.cpp's
                    // passive decode path, see its comment for the full
                    // story). Confirm the command completed either way, just
                    // don't feed a bogus value into the cover's real state.
                    ESP_LOGI(TAG, "2W command to %02X%02X%02X confirmed", src[0], src[1], src[2]);
                    if (raw <= 0xC800 && cover != nullptr) {
                        float closure_percent = raw / 512.0f;
                        ESP_LOGI(TAG, "  real closure=%.0f%%", closure_percent);
                        cover->update_real_position_authoritative(closure_percent);
                    }
                    return true;
                }

                // GET_NAME's answer (0x51) rather than an ordinary command's
                // (0x04) - a real reply here is strong end-to-end
                // confirmation of the whole crypto chain (derived
                // system_key, challenge/response), not just that bonding
                // completed. Logged at ESP_LOGW so it's never missed
                // regardless of Debug Logging state, same discipline as the
                // passive key-sniff success log.
                if (s.cmd_state == CmdState::ANSWERED_WAITING_ACK && s.last_sent_cmd == 0x50 && cmd == 0x51 &&
                    packet->buffer_length >= static_cast<int>(sizeof(_header) + sizeof(_p0x51))) {
                    s.cmd_state = CmdState::IDLE;
                    std::string name(packet->payload.packet.msg.p0x51.name, sizeof(_p0x51::name));
                    // Truncate at the first NUL, if any, for a clean log line -
                    // the real device otherwise pads with trailing garbage/
                    // spaces up to the fixed 16-byte width.
                    size_t nul_pos = name.find('\0');
                    if (nul_pos != std::string::npos)
                        name.resize(nul_pos);
                    ESP_LOGW(TAG, "GET_NAME (0x51) from %02X%02X%02X: \"%s\"", src[0], src[1], src[2], name.c_str());
                    return true;
                }
            }
        }

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
            case BondState::WAITING_DISCOVER_RESP:
                if (cmd != 0x29) return false;
                ESP_LOGI(TAG, "Received DISCOVER_RESP (0x29) from %02X%02X%02X - sending DISCOVER_CONFIRMATION",
                         source[0], source[1], source[2]);
                // Discovery's RX side may have been hopped to CH1/CH3 when
                // this arrived - send_discover_confirmation() retunes to CH2
                // itself before transmitting (Finding 22 - all TX is
                // CH2-only).
                send_discover_confirmation(source);
                attempt_.state = BondState::WAITING_CONFIRM_ACK;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::WAITING_CONFIRM_ACK:
                if (cmd != 0x2D) return false;
                ESP_LOGI(TAG, "Received DISCOVER_CONFIRMATION_ACK (0x2D) from %02X%02X%02X - sending KEY_INIT",
                         source[0], source[1], source[2]);
                send_key_init(source);
                attempt_.state = BondState::SENT_KEY_INIT;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::SENT_KEY_INIT:
                // Most devices challenge with 0x3C; some accept immediately
                // with a direct 0x33 (Finding 20 / home_io_control's own
                // wait_for_key_challenge_() comment - observed mostly when
                // the controller's TX->RX turnaround is slow enough that
                // the 0x3C gets missed). Accept either.
                if (cmd == 0x33) {
                    finalize_bonding(source);
                    return true;
                }
                if (cmd != 0x3C) return false;
                ESP_LOGI(TAG, "Received CHALLENGE_REQ (0x3C) - sending KEY_TRANSFER");
                send_key_transfer(source, packet->payload.packet.msg.p0x3c.challenge);
                attempt_.state = BondState::SENT_KEY_TRANSFER;
                attempt_.state_entered_ms = esphome::millis();
                return true;

            case BondState::SENT_KEY_TRANSFER:
                if (cmd != 0x33) return false;
                finalize_bonding(source);
                return true;

            default:
                return false;
        }
    }
}
