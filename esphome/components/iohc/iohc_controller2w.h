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
// 3 section and /config/.claude/io-2w-protocol.md (Findings 12-20) for the
// full research this is built on.
//
// REDESIGNED 2026-07-11 (Finding 20): every earlier attempt at this class
// assumed the MOTOR sends 0x28 DISCOVER spontaneously when its physical
// pairing button is pressed, with the box only ever replying
// (0x29/0x2C/0x32/...). That assumption is backwards, and is exactly why
// three independent real-hardware tests (Findings 12/15/17) found nothing.
// Confirmed by reading github.com/laberning/home_io_control - a mature,
// independently-tested ESPHome IO-Homecontrol component with confirmed
// real 2W pairing on the same SX1276 chip family - the CONTROLLER
// broadcasts 0x28 DISCOVER; a device already in pairing mode (physical
// button already pressed, same as before) just listens and answers with
// 0x29. The rest of bonding (0x31 KEY_INIT -> 0x3C CHALLENGE_REQ -> 0x32
// KEY_TRANSFER -> 0x33 KEY_CONFIRM) is also controller-initiated. See
// io-2w-protocol.md Finding 20 for the full byte-level writeup, including
// the real crypto bug this also exposed: per-command authentication (and
// the KEY_TRANSFER payload itself) needs a real per-installation
// `system_key`, not the fixed/public transfer_key this class used
// everywhere before.
//
// Everything downstream of bonding (the per-command challenge/response
// frame shape) was, and remains, thoroughly confirmed against many real
// TaHoma captures - only the bonding sequence itself and the crypto key
// used for ongoing authentication were wrong.
//
// REVISED AGAIN 2026-07-12 (Finding 22): Finding 20's redesign still got
// zero 0x29 replies on two different motors, both with real-hardware-
// confirmed open pairing windows (physical jog). Root cause found by
// reading home_io_control's proto_timing.h directly rather than just its
// higher-level docs: "Commands are sent on CH2; responses may arrive on
// any channel." Finding 20/21's implementation hopped the TRANSMIT channel
// for 0x28 across CH1/CH2/CH3 - backwards. The reference always transmits
// on CH2 and only hops the RECEIVE side while waiting for a reply (at a
// ~2.7ms ISR-driven cadence in their design - impractical to match exactly
// in this bridge's own architecture without touching the ISR layer, but a
// much faster loop()-driven RX hop than Finding 21's crude 5s-per-channel
// dwell is a real, tractable improvement). Also added Listen-Before-Talk
// (LBT) before every 2W transmission - proto_timing.h documents this as a
// real ETSI EN 300 220 compliance mechanism the reference implements and
// this bridge never had at all, and this session already captured direct
// evidence of real RF contention (TaHoma's own traffic landing mid-attempt
// during a bonding test).

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
class IOHCComponent;
}  // namespace iohc
}  // namespace esphome

namespace IOHC {

    enum class BondState : uint8_t {
        IDLE,
        WAITING_DISCOVER_RESP,  // sent (or re-sent) our own 0x28, waiting for the motor's 0x29
        // Confirmed required by a second independent reference
        // (nicolas5000/io-rts-esp32's DiscoverAndPairDevice() - the whole
        // pairing aborts if this step fails) - inserted here per that
        // implementation, even though it can't explain why 0x28 itself has
        // never once been answered on this install (this step only matters
        // after receiving a 0x29, which has never happened).
        WAITING_CONFIRM_ACK,    // sent 0x2C, waiting for the motor's 0x2D
        SENT_KEY_INIT,          // sent 0x31, waiting for the motor's 0x3C challenge (or an early 0x33)
        SENT_KEY_TRANSFER,      // sent 0x32 (real system_key, obfuscated), waiting for 0x33
        BONDED,
        FAILED,
    };

    enum class CmdState : uint8_t {
        IDLE,
        SENT_WAITING_CHALLENGE,   // command sent, waiting for the motor's 0x3C
        ANSWERED_WAITING_ACK,     // 0x3D sent, waiting for the motor's 0x04 answer
    };

    // Passive key-sniffing (Findings 27/28) - completely independent of
    // this bridge's own bonding attempt_ FSM above. Watches ALL 2W traffic
    // (any source/destination) for a real KEY_INIT(0x31)->
    // CHALLENGE_REQUEST(0x3C)->KEY_TRANSFER(0x32) sequence between two
    // OTHER devices (e.g. TaHoma bonding with a motor), to recover that
    // controller's real per-installation system_key. EXPERIMENTAL/
    // diagnostic tool - not used by any of this bridge's own normal
    // operation (bonding, control, passive position decode).
    enum class SniffState : uint8_t {
        IDLE,             // armed, but haven't seen a KEY_INIT yet
        SAW_KEY_INIT,     // saw 0x31 box->device, waiting for matching 0x3C device->box
        SAW_CHALLENGE,    // saw matching 0x3C, waiting for matching 0x32 box->device
    };

    // Per-motor bonded session. Keyed by the motor's real (packed) 2W
    // address, NOT this bridge's own 1W node/key identity for the same
    // physical cover - those are two completely separate identities/trust
    // relationships, same distinction IOHCCover already documents between
    // node/key and motor_address.
    struct Session2W {
        bool bonded{false};
        // The real, per-installation system_key (see Finding 20), copied
        // in from IOHCController2W::system_key_ once bonding completes.
        // Every motor bonded to this controller shares the SAME key -
        // system_key is a controller/hub-level secret, not per-motor - but
        // it's still stored per-session for a simple, uniform lookup at
        // per-command answer time (Session2W is already keyed by motor).
        uint8_t key[16]{};
        uint8_t last_sent_cmd{0};
        std::vector<uint8_t> last_sent_data;
        CmdState cmd_state{CmdState::IDLE};
        uint32_t cmd_sent_ms{0};
        // Set by send_command() for the duration of one in-flight command
        // only - used to route the eventual 0x04 answer's real position back
        // to the cover that asked for it, via update_real_position_authoritative()
        // (Phase 3d, distinct from the existing passive-only
        // update_real_position() - this path is actively solicited and
        // authenticated via the live challenge/response, so it's allowed to
        // touch the cover's real position/state, unlike unvalidated passive
        // decode - see Finding 10's rule).
        esphome::iohc::IOHCCover *pending_cmd_cover{nullptr};
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
        // owner is used only to request the bridge-wide fast RX hop for the
        // duration of a bonding attempt (Finding 32, set_bonding_hop_wanted())
        // - may be nullptr in contexts that don't need that (e.g. tests).
        // fixed_controller_hex/fixed_system_key_hex: optional, YAML-provided
        // identity - leave both empty to keep the default random-generate-
        // and-persist-on-device behavior (see iohc/__init__.py's
        // controller_address/system_key config comment for the same
        // survives-a-board-replacement rationale already used for each
        // cover's own node/key).
        void begin(iohcRadio *radio, esphome::iohc::IOHCComponent *owner, const std::string &fixed_controller_hex = "",
                   const std::string &fixed_system_key_hex = "");

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
        // file: never treat any 0x29 as a match, only one from the specific
        // motor this cover already claims via its existing motor_address
        // config. Actively transmits our own 0x28 DISCOVER broadcast on
        // CH2 (Finding 22 - always CH2 for TX, never hopped) - RX coverage
        // across CH1/CH2/CH3 comes from the bridge-wide cooperative hop
        // (Finding 31, esphome::iohc::IOHCComponent::maybe_hop_()), not
        // anything this class manages itself anymore. The motor must
        // already be in its physical pairing window (hold PROG on an
        // existing paired remote first, same as before) for it to answer.
        void arm_bonding(const IOHC::address &motor_address, esphome::iohc::IOHCCover *cover);

        // Phase 3d: sends a real 2W movement command (Open/Close/Position/
        // Stop, same main-byte formula already confirmed for 1W) to an
        // already-bonded motor, then waits for the live 0x3C/0x3D challenge/
        // response before the motor will act on it - matches every real
        // TaHoma capture of this exchange (io-2w-protocol.md Findings 5-9).
        // No-op with a warning if the motor isn't bonded yet, or if a
        // command is already in flight to it. `cover` is used only to route
        // the eventual real position answer back - see Session2W's
        // pending_cmd_cover comment.
        void send_command(const IOHC::address &to, uint8_t main0, uint8_t main1, esphome::iohc::IOHCCover *cover);

        bool is_bonded(const IOHC::address &motor_address) const;

        // Opt-in diagnostic (not part of bonding itself - confirmed via
        // laberning/home_io_control's own pairing_engine.cpp that naming is
        // deliberately out of scope for its discover_and_pair() sequence
        // too, just a hardcoded placeholder logged for the operator to edit
        // by hand). Sends GET_NAME (0x50, crypto-wrapped, same
        // challenge/response shape as an ordinary command) to an
        // already-bonded motor and logs whatever name comes back - a real
        // 0x51 reply is a strong end-to-end confirmation the whole crypto
        // chain (derived system_key, challenge/response) actually works,
        // not just that bonding completed. No-op with a warning if not
        // bonded yet, or if a command is already in flight.
        void send_get_name(const IOHC::address &to);

        // Passive 2W key sniffing (Findings 27/28/31) - address-agnostic and
        // hub-scoped: unlike arm_bonding(), does not target one specific
        // motor. Watches every 2W frame this bridge overhears, from anyone,
        // addressed to anyone, for the bonding-only KEY_INIT/
        // CHALLENGE_REQUEST/KEY_TRANSFER sequence, unconditionally, from
        // handle_frame() - no separate arm/disarm step anymore. Now that RX
        // coverage across all 3 channels is the bridge-wide default
        // (Finding 31), there's no reliability cost to running this
        // continuously, the same way the existing Target Closure/RSSI
        // decode already runs continuously. If a complete sequence is
        // captured, the derived system_key is logged (hex, ESP_LOGW so it's
        // never missed even with Debug Logging off) - there is no dedicated
        // HA entity for it.

    private:
        static uint32_t pack_address(const IOHC::address &addr);

        void load_or_generate_identity(const std::string &fixed_controller_hex);
        void load_or_generate_system_key(const std::string &fixed_system_key_hex);
        void reset_bonding(BondState to_state, const char *reason);
        void finalize_bonding(const IOHC::address &motor);
        void reset_sniff_state();

        void send_discover();
        // CMD 0x2C DISCOVER_CONFIRMATION - unicast, EndFrame=0/LowPower=1
        // (matching nicolas5000/io-rts-esp32's create_discovery_confirmation_request()
        // exactly, byte-verified against its real source - deliberately
        // different header flags from every other 2W frame this class
        // sends, which all use forge_2w_packet()'s EndFrame=1/LowPower=0
        // defaults).
        void send_discover_confirmation(const IOHC::address &to);
        void send_key_init(const IOHC::address &to);
        void send_key_transfer(const IOHC::address &to, const uint8_t *challenge6);
        void send_set_config1(const IOHC::address &to);
        // Best-effort, fire-and-forget (see _p0x52's own comment in
        // iohcPacket.h for why this isn't wrapped in challenge/response).
        // Truncates name to 15 chars + a trailing NUL, matching the
        // confirmed 16-byte real payload shape.
        void send_set_name(const IOHC::address &to, const std::string &name);

        // Listen-Before-Talk (Finding 22) - checks RSSI is below threshold
        // before a 2W transmission, backing off briefly and re-checking up
        // to a bounded number of times; transmits anyway after that (never
        // blocks forever). Matches home_io_control's proto_timing.h ETSI EN
        // 300 220 constants. Called from every 2W send_*() below, right
        // before radio_->send() - centralized here rather than in
        // forge_2w_packet() since LBT needs the radio in RX/idle state to
        // read a meaningful RSSI, which is only true immediately before
        // send, not at packet-construction time.
        void listen_before_talk();

        iohcRadio *radio_{};
        esphome::iohc::IOHCComponent *owner_{nullptr};
        ::Preferences identity_prefs_;
        IOHC::address controller_address_{};
        // Real per-installation secret (Finding 20), generated once via
        // esp_random() and persisted the same way as controller_address_
        // above - shared by every motor this controller ever bonds with,
        // analogous to how one TaHoma box uses one system key across all
        // 14 shutters on this install.
        uint8_t system_key_[16]{};

        struct BondAttempt {
            BondState state{BondState::IDLE};
            IOHC::address target_motor{};
            esphome::iohc::IOHCCover *armed_cover{nullptr};
            uint32_t state_entered_ms{0};
            // WAITING_DISCOVER_RESP only - re-sending the 0x28 itself
            // (always on CH2 - see send_discover()). RX coverage across
            // CH1/CH2/CH3 in between sends comes from the bridge-wide
            // cooperative hop (Finding 31) - no per-attempt hop state
            // needed here anymore.
            uint32_t last_discover_sent_ms{0};
            uint8_t discover_retries{0};
        } attempt_;

        std::unordered_map<uint32_t, Session2W> sessions_;  // keyed by pack_address(motor)

        struct KeySniff {
            SniffState state{SniffState::IDLE};
            IOHC::address box{};      // source of the observed 0x31 (the controller doing the bonding)
            IOHC::address device{};   // destination of the observed 0x31 (the motor being bonded)
            uint8_t challenge[6]{};   // captured from the matching 0x3C
        } sniff_;
    };
}
#endif
