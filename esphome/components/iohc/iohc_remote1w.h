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

// Trimmed port of upstream's iohcRemote1W (renamed iohc_remote1w to fit this
// component's flat-directory naming convention - see iohc.h). Upstream's
// class is a singleton that owns a JSON-loaded (LittleFS + ArduinoJson) list
// of many virtual remotes, plus MQTT/webserver publishing hooks. None of
// that fits ESPHome: here, each ESPHome `cover:` entry gets its own
// IOHCRemote1W instance (one virtual remote identity per physical motor,
// exactly like upstream's own one-entry-per-bonded-device model - see
// addRemote() upstream), configured from YAML instead of a JSON file, with
// its bonded identity (node address + key) and sequence number persisted via
// Arduino's Preferences (the same NVS wrapper upstream's own
// nvs_helpers.cpp uses) instead of a JSON file + separate NVS calls.
//
// Frame-building logic (forgePacket, the Open/Close/Stop/Position/Add/Pair/
// Remove cases) is ported near-verbatim from iohcRemote1W::cmd() - only the
// persistence and multi-remote bookkeeping around it changed.

#ifndef IOHC_REMOTE1W_H
#define IOHC_REMOTE1W_H

#include <Preferences.h>
#include <string>
#include "iohcRadio.h"
#include "iohcPacket.h"
#include "iohc_blind_position.h"

namespace IOHC {

    enum class RemoteButton {
        Pair,
        Add,
        Remove,
        Open,
        Close,
        Stop,
        Position,
        // "My"/favorite position while idle - NOT the same as Stop. Confirmed
        // via a real TaHoma "My" capture: TaHoma sends main=0xd8 (this is
        // upstream's own RemoteButton::Vent case, main[1]=0x03), never
        // main=0xd2 (Stop). Stop only has an effect while actually moving -
        // sending it while idle is a no-op on the motor side, which is why
        // "My" previously did nothing when the cover was already stationary.
        Vent,
        // 0x1E: identify/locate. No physical remote has this button - only
        // seen via a real captured TaHoma/2W frame. Overkiz exposes it as 3
        // separate high-level actions (identify/startIdentify/stopIdentify),
        // confirmed via pyoverkiz's own command enum - matched 1:1 here.
        Identify,
        StartIdentify,
        StopIdentify,
        // The single "Prog" button's actual entry point. Add (0x30) and
        // Remove (0x39) are NOT a motor-side toggle - checked directly
        // against upstream's own reference: they're two structurally
        // distinct commands, each unconditionally doing what its name says
        // (Add never removes, Remove never adds). A real physical remote
        // decides locally, from its own remembered pairing status, which one
        // to actually send when its PROG button is pressed - it has no way
        // to query the motor's live state. Prog reproduces that: it reads
        // our own persisted paired_ flag and dispatches to Add or Remove
        // accordingly, same as a real remote would.
        Prog,
    };

    // One virtual remote identity, bonded 1:1 with one physical motor.
    // Broadcast "type" is the device-class group the motor listens on (see
    // sDevicesType in iohc_utils.h) - default 0 ("All") matches upstream's
    // own addRemote() default, confirmed working for Add/Remove against
    // real shutter motors. Deliberately YAML-configurable (not hardcoded)
    // in case a different device type ever needs a different group.
    class IOHCRemote1W {
    public:
        // pref_namespace must be unique per cover (e.g. the cover's object_id)
        // so each motor's bonded identity/sequence persists independently.
        //
        // fixed_node_hex/fixed_key_hex (6/32 hex chars) are optional. If both
        // are given, this identity comes straight from YAML/secrets.yaml
        // instead of being randomly generated into this board's own flash -
        // the point being that a replacement board flashed with the same
        // config reproduces the exact same bonded identity, so motors still
        // recognize it and nothing needs re-pairing. If left empty (the
        // original behavior), a random identity is generated on first boot
        // and persists only in this board's NVS - fine until the board dies.
        void begin(iohcRadio *radio, const std::string &pref_namespace, const std::string &fixed_node_hex = "",
                   const std::string &fixed_key_hex = "");

        void set_travel_time_open(uint32_t seconds) { position_tracker_.setTravelTimeOpen(seconds); }
        void set_travel_time_close(uint32_t seconds) { position_tracker_.setTravelTimeClose(seconds); }
        void set_type(uint8_t type) { type_ = type; }
        void set_manufacturer(uint8_t manufacturer) { manufacturer_ = manufacturer; }

        bool is_paired() const { return paired_; }
        BlindPosition &position_tracker() { return position_tracker_; }

        // percent is only used for RemoteButton::Position (0-100).
        void cmd(RemoteButton button, int percent = -1);

    private:
        void forge_packet(iohcPacket *packet);
        void load_or_generate_identity();
        void bump_and_persist_sequence();

        iohcRadio *radio_{};
        Preferences prefs_;
        std::string pref_namespace_;
        bool has_fixed_identity_{false};

        address node_{};      // our virtual remote's own address (Source)
        uint8_t key_[16]{};    // shared session key, established at Add time
        uint16_t sequence_{1};
        uint8_t type_{0};
        uint8_t manufacturer_{2}; // 2 = Somfy, matches upstream's own default
        bool paired_{false};

        BlindPosition position_tracker_{};
    };
}
#endif
