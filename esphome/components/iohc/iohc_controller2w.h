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

// Phase 3 - this bridge acting as a real 2W controller (the role TaHoma
// plays), not just passively decoding another controller's traffic (that
// existing feature is IOHCCover::update_real_position(), unrelated to this
// file). See /root/.claude/plans/i-bought-a-lilygo-clever-planet.md's Phase
// 3 section and /config/.claude/io-2w-protocol.md (Findings 12-14) for the
// full research this is built on.
//
// IMPORTANT, unlike iohc_remote1w.h: the actual bonding sequence
// (DISCOVER/LAUNCH_KEY_TRANSFERT/KEY_TRANSFERT) has never been observed on
// this install despite a full real delete+re-add cycle via the TaHoma app
// (Finding 14) - TaHoma's own 2W bond survives app-level "delete", so it
// never had to demonstrate a fresh bonding ceremony. This class's bonding
// FSM is therefore built on upstream's exploratory main.cpp as the best
// available hypothesis, NOT on a real capture of this exact bridge's own
// exchange with a motor - it is expected to need real-hardware debugging
// the first time it's actually tried, not just a translation exercise.
// Everything downstream of bonding (the per-command challenge/response
// shape) is, by contrast, thoroughly confirmed against many real captures.

#ifndef IOHC_CONTROLLER2W_H
#define IOHC_CONTROLLER2W_H

#include <Preferences.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "iohcRadio.h"
#include "iohcPacket.h"

namespace esphome {
namespace iohc {
class IOHCCover;
}  // namespace iohc
}  // namespace esphome

namespace IOHC {

    enum class BondState : uint8_t {
        IDLE,
        ARMED_WAITING_DISCOVER,       // user pressed "Program (2W)", waiting for the motor's own 0x28
        SENT_DISCOVER_ANSWER,         // sent 0x29, waiting for the next step - unconfirmed sequence past here, see header note
        SENT_DISCOVER_ACTUATOR,       // sent 0x2C, waiting for 0x2D
        WAITING_LAUNCH_KEY_TRANSFERT, // got 0x2D, waiting for the motor's 0x38
        SENT_KEY_TRANSFERT,           // sent 0x32, waiting for a 0x3C/0x3D round trip to confirm
        BONDED,
        FAILED,
    };

    enum class CmdState : uint8_t {
        IDLE,
        SENT_WAITING_CHALLENGE,   // command sent, waiting for the motor's 0x3C
        ANSWERED_WAITING_ACK,     // 0x3D sent, waiting for the motor's 0x04 answer
    };

    // Per-motor bonded session. Keyed by the motor's real (packed) 2W
    // address, NOT this bridge's own 1W node/key identity for the same
    // physical cover - those are two completely separate identities/trust
    // relationships, same distinction IOHCCover already documents between
    // node/key and motor_address.
    struct Session2W {
        bool bonded{false};
        // Whether a real per-motor secret is even part of this protocol is
        // itself unresolved (see io-2w-protocol.md's addendum to Finding 6,
        // and Finding 14's second confirmation) - stored here in case it
        // turns out to be real and gets learned during a genuine bonding
        // ceremony; unused (all zero) until then.
        uint8_t key[16]{};
        uint8_t last_sent_cmd{0};
        std::vector<uint8_t> last_sent_data;
        CmdState cmd_state{CmdState::IDLE};
        uint32_t cmd_sent_ms{0};
    };

    // The box/controller role - one shared instance per bridge (see
    // esphome::iohc::IOHCComponent::controller2w()), not one per cover the
    // way IOHCRemote1W is: a single 2W controller identity can be bonded to
    // many motors, exactly like TaHoma itself is one box serving 14
    // shutters. Owns the bonding FSM (one attempt at a time - bonding is a
    // deliberate, user-supervised action, not concurrent background work)
    // and a per-motor Session2W map for anything already bonded.
    class IOHCController2W {
    public:
        void begin(iohcRadio *radio, const std::string &fixed_controller_hex = "");

        // Timeout/delayed-send draining only - see iohc_controller2w.cpp's
        // loop() for why this exists instead of wiring iohcRadio's own
        // `delayed` field (deliberate choice, see the plan file's Phase 3
        // "delayed field decision" section).
        void loop();

        // Called from esphome::iohc::IOHCComponent::on_receive() for every
        // 2W frame. Returns true if this class consumed the frame (either
        // routed to the in-progress bonding attempt, or to an in-flight
        // command's challenge/response), false if the caller should keep
        // treating it as ordinary passive-decode traffic (the existing,
        // unrelated Tier 1 feature - this must never interfere with that).
        bool handle_frame(IOHC::iohcPacket *packet);

        // User-triggered entry point (see esphome::iohc::IOHCCover::press_prog2w()).
        // Fails fast (logs a warning, no-op) if motor_address is all-zero -
        // see the header's "scoping DISCOVER matches" rationale in the plan
        // file: never listen for "any" 0x28, only the specific motor this
        // cover already claims via its existing motor_address config.
        void arm_bonding(const IOHC::address &motor_address, esphome::iohc::IOHCCover *cover);

        bool is_bonded(const IOHC::address &motor_address) const;

    private:
        static uint32_t pack_address(const IOHC::address &addr);

        void load_or_generate_identity(const std::string &fixed_controller_hex);
        void reset_bonding(BondState to_state, const char *reason);

        void send_discover_answer(const IOHC::address &to);
        void send_discover_actuator(const IOHC::address &to);
        void send_key_transfert(const IOHC::address &to, const uint8_t *challenge6);

        iohcRadio *radio_{};
        ::Preferences identity_prefs_;
        IOHC::address controller_address_{};

        struct BondAttempt {
            BondState state{BondState::IDLE};
            IOHC::address target_motor{};
            esphome::iohc::IOHCCover *armed_cover{nullptr};
            uint32_t state_entered_ms{0};
        } attempt_;

        std::unordered_map<uint32_t, Session2W> sessions_;  // keyed by pack_address(motor)
    };
}
#endif
