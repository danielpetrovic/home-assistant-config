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

#ifndef IOHC_PACKET_H
#define IOHC_PACKET_H

#include <vector>
#include <string>

#include "iohc_board_config.h"

#if defined(RADIO_SX127X)
#include "SX1276Helpers.h"
#endif

#define RESET_AFTER_LAST_MSG_US         15000
#define MAX_FRAME_LEN                   32
#define IOHC_INBOUND_MAX_PACKETS        255     // Maximum Inbound packets buffer
#define IOHC_OUTBOUND_MAX_PACKETS       20      // Maximum Outbound packets

namespace IOHC {
    typedef uint8_t address[3];

    struct CB1 {
        uint8_t MsgLen: 5; //1
        uint8_t Protocol: 1; //2
        uint8_t StartFrame: 1; //3
        uint8_t EndFrame: 1; //4
    };

    struct CB2 {
        uint8_t Version: 2;
        uint8_t Prio: 1;
        uint8_t Unk2: 1;
        uint8_t Unk3: 1;
        uint8_t LPM: 1;
        uint8_t Routed: 1;
        uint8_t Beacon: 1;
    };

    union CtrlByte1Union {
        uint8_t asByte;
        CB1 asStruct;
    };

    union CtrlByte2Union {
        uint8_t asByte;
        CB2 asStruct;
    };

    /// Common frame header
    struct _header {
        CtrlByte1Union CtrlByte1; //1
        CtrlByte2Union CtrlByte2; //1
        address target; //3
        address source; //3
        uint8_t cmd; //1
    };

    struct Acei {
        uint8_t isvalid: 1;
        uint8_t extended: 2;
        uint8_t service: 2;
        uint8_t level: 3;
    };

    union AceiUnion {
        uint8_t asByte;
        Acei asStruct;
    };

    //61 0110 0001
    //83 1000 0011
    inline void setAcei(AceiUnion&acei, uint8_t value) {
        acei.asByte = value;
        // acei.level = (value >> 5) & 0x07;
        // acei.service = (value >> 3) & 0x03;
        // acei.extended = (value >> 1) & 0x03;
        // acei.isvalid = value & 0x01;
    }

    struct _p0x00_16 {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main[2];
        uint8_t fp1;
        uint8_t fp2;
        uint8_t data[2];
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    // 2W CMD 0x00 (Activate/Execute) - box -> motor, no HMAC (2W leans on the
    // live 0x3C/0x3D challenge/response for authentication instead, unlike
    // 1W's self-signed frames). 6 bytes total, confirmed against many real
    // captures (io-2w-protocol.md Findings 5-9): org=0x01 (User), acei=0xE7,
    // main = 0x0000 (Open) / 0xC800 (Close) / 0xD200 (Stop) / (100-p)*2 for
    // Position, matching the exact same formula already used for 1W. The
    // trailing 2 bytes are always 0x00 in every real capture - purpose
    // unknown, mirrored as-is rather than assumed to be meaningful.
    struct _p0x00_2w {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main[2];
        uint8_t reserved[2];
    };

    struct _p0x00_14 {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main[2];
        uint8_t fp1;
        uint8_t fp2;
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    struct _p0x01_13 {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main; //[2];
        uint8_t fp1;
        uint8_t fp2;
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    struct _p0x20_15 {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main[2];
        uint8_t fp1;
        uint8_t fp2;
        uint8_t fp3;
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    struct _p0x20_13 {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main[2];
        uint8_t fp1;
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    struct _p0x20_16 {
        uint8_t origin;
        AceiUnion acei;
        uint8_t main[2];
        uint8_t fp1;
        uint8_t fp2;
        uint8_t data[2];
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    struct _p0x2b {
        uint8_t actuator[2];
        address backbone;
        uint8_t manufacturer;
        uint8_t info;
        uint8_t tstamp[2];
    };
/// 1) 1W PAIR
    struct _p0x2e {
        uint8_t data;
        uint8_t sequence[2];
        uint8_t hmac[6];
    };
/// 3) 1W ADD**
    struct _p0x30 {
        uint8_t enc_key[16];
        uint8_t man_id;
        uint8_t data;
        uint8_t sequence[2];
    };

    // 0x1E: Identify/locate. No 1W reference documents this (no physical
    // remote has an Identify button) - a real captured 2W TaHoma frame uses
    // a bare 2-byte payload with no HMAC (security instead comes from the
    // live challenge/response round trip 2W does separately). This struct is
    // our own best-guess 1W-broadcast equivalent, self-signed the same way
    // as _p0x2e (Pair/Remove), since a 1W remote has no round trip to lean
    // on - unconfirmed against real hardware.
    struct _p0x1e {
        uint8_t data[2];
        uint8_t sequence[2];
        uint8_t hmac[6];
    };

    // --- 2W bonding-family structs (corrected per Finding 20) ---
    // The confirmed real sequence (controller-initiated, per
    // github.com/laberning/home_io_control - a mature, independently-tested
    // ESPHome IO-Homecontrol component with confirmed real 2W pairing on
    // the same SX1276 chip family, see io-2w-protocol.md Finding 20) is:
    //   Controller -> 0x28 DISCOVER (broadcast, 0-byte)
    //   Device     -> 0x29 DISCOVER_RESP (device metadata, see _p0x29_resp)
    //   Controller -> 0x31 KEY_INIT (0-byte)
    //   Device     -> 0x3C CHALLENGE_REQ (6-byte, see _p0x3c below)
    //   Controller -> 0x32 KEY_TRANSFER (16-byte, see _p0x32 below)
    //   Device     -> 0x33 KEY_CONFIRM (0-byte)
    //   Controller -> 0x6F SET_CONFIG1 (best-effort, see _p0x6f below)
    // 0x28, 0x31, 0x33: all 0-byte payloads - no struct needed, the
    // `dataLen != 0` guard in decode() already skips field access for
    // these; the `[cmd2w_name]` label alone is enough to identify them in
    // the log. This supersedes the earlier (wrong-direction) hypothesis
    // that the MOTOR sends 0x28 spontaneously and the box replies with
    // 0x29/0x2C/etc - see io-2w-protocol.md Finding 20 for the full
    // correction and why every prior attempt based on that assumption
    // (Findings 12/15/17) found nothing.

    // 0x29 DISCOVER_RESP: device's reply to our 0x28, up to 9 bytes of
    // metadata. We already know which motor we're bonding with from this
    // cover's configured motor_address (unlike a from-scratch discovery
    // controller that has to learn the address from this frame alone), so
    // only the source address (parsed from the frame header, not this
    // struct) is load-bearing for us - the fields below are logged for
    // diagnostics only, not currently acted on.
    struct _p0x29_resp {
        uint8_t type_subtype[2];  // packed device type/subtype
        address backbone;         // backbone address
        uint8_t manufacturer;     // 1=Velux 2=Somfy 3=Honeywell ... 11=Overkiz, see proto_constants.h manufacturer table
        uint8_t flags;            // "Multi Information Byte": turnaround class, power-save mode, etc.
        uint8_t timestamp[2];
    };

    // 0x6F SET_CONFIG1: best-effort, sent once after bonding completes to
    // ask the device to auto-broadcast status updates to us. Fixed payload
    // confirmed from home_io_control's own working controller captures.
    struct _p0x6f {
        uint8_t data[5];  // {0xE0, 0x10, 0x0A, 0x08, 0x00}
    };

    // 0x32 KEY_TRANSFERT: box -> motor, 16-byte AES-ECB(transfert_key, IV)
    // XOR transfert_key, per upstream's RECEIVED_LAUNCH_KEY_TRANSFERT_0x38
    // handler.
    struct _p0x32 {
        uint8_t encrypted_key[16];
    };

    // 0x3C CHALLENGE_REQUEST / 0x3D CHALLENGE_ANSWER: both carry a 6-byte
    // challenge/response value in the exchanges captured so far (per
    // io-2w-protocol.md Finding 5-7) - upstream's own 0x3D can also be
    // 16 bytes in the bonding-specific case (echoing KEY_TRANSFERT instead
    // of a plain challenge answer), so treat dataLen as authoritative over
    // this struct's fixed size until confirmed.
    struct _p0x3c {
        uint8_t challenge[6];
    };
    struct _p0x3d {
        uint8_t response[6];
    };

    // 0x03: box -> motor query, 3-byte payload {0x03, 0x00, 0x00} per
    // docs/commands.md as quoted in io-2w-protocol.md - not yet captured
    // directly by this component.
    struct _p0x03 {
        uint8_t data[3];
    };

    // 0x04 Private Command Answer: fields confirmed against a real, clean
    // capture (addendum to Finding 6, io-2w-protocol.md - our own bridge
    // moved a cover over 1W, TaHoma reactively re-verified over 2W ~1s
    // later) as well as the earlier Finding 6/7/9 captures. Layout: status byte, one
    // not-yet-understood byte, the Main/position echo (raw/2 = closure %,
    // matches the CMD 0x00 movement command's own Main parameter, confirmed
    // exactly against the "Target Closure sensor updated... 20%" log line),
    // 4 further unmapped bytes, then a 3-byte address that echoes back
    // whichever controller most recently commanded the motor - confirmed
    // NOT always TaHoma's own box address: in Finding 13's capture this
    // held our own bridge's 1W identity, since our bridge was the one that
    // actually sent the move. Earlier findings assumed "box address echo"
    // because those captures only ever involved TaHoma-initiated moves,
    // where the two addresses happen to coincide. `iohc.cpp`'s existing
    // passive-decode path reads buffer[11]/[12] directly (= this struct's
    // `main[0]`/`main[1]`) rather than using this struct - kept that way
    // deliberately for now, see Phase 3a notes before consolidating.
    struct _p0x04_14 {
        uint8_t status;
        uint8_t unknown1;
        uint8_t main[2]; // position echo, raw/2 = closure %
        uint8_t unmapped1[4];
        address commanding_controller; // last controller to command a move - NOT necessarily TaHoma's own address
        uint8_t unmapped2[3];
    };

    // --- Confirmed by Finding 14 (real, complete TaHoma delete+re-add
    // capture, io-2w-protocol.md) - these are genuinely observed, not
    // hypotheses derived from upstream's exploratory code. ---

    // 0x2A DISCOVER_REMOTE: box broadcasts this (to a broadcast-style
    // target, e.g. "003B") searching for a remote-type device. 12-byte
    // payload, purpose of the bytes not understood - captured only as a
    // repeated, unanswered broadcast (4 retries over ~5s, no 0x2B
    // DISCOVER_REMOTE_ANSWER seen in response) so the answer's shape and
    // whether the request is crypto-wrapped are both still unknown.
    struct _p0x2a {
        uint8_t data[12];
    };

    // 0x2E / 0x2F in a **2W context**: distinct from 1W's own 0x2E
    // (Pair/learning marker, see _p0x2e above) - this is a crypto-verified
    // device-confirmation ping used by the box to roll-call its known
    // devices. Box sends 0x2E DATA=0x02, device answers 0x3C challenge,
    // box answers 0x3D, device confirms with 0x2F DATA=0x02. Observed
    // across 11 different devices in one roll-call, same 1-byte payload
    // (0x02) every time - purpose of that specific value unconfirmed
    // (could be a fixed marker, or a type code that happens to be constant
    // across this install's devices).
    struct _p0x2e_2w {
        uint8_t data;
    };
    struct _p0x2f {
        uint8_t data;
    };

    // 0x19: brand new command, not present in iohcDevice.h's commandId
    // enum at all. 1-byte payload (0x03 observed), wrapped in the same
    // 0x3C/0x3D challenge/response, answered via 0xFE STATUS (0x05 =
    // success) rather than its own dedicated answer code. Seen only
    // immediately after a device was newly added to TaHoma's roster
    // (right after its first 0x2E/0x2F roll-call confirmation) - purpose
    // still unclear beyond "part of registering/activating a
    // newly-(re)discovered device." Name unknown, flagged as
    // PRIVATE_UNKNOWN_0x19 pending a better name.
    struct _p0x19 {
        uint8_t data;
    };

    // 0x51 GET_NAME answer (2W context): 16-byte plain ASCII device name,
    // e.g. "Living Room Shut" (truncated at 16 bytes - presumably "Living
    // Room Shutter"). Matches the command byte already used by the
    // existing 1W GET_NAME implementation in iohc_remote1w.cpp, but the 2W
    // version requires the 0x3C/0x3D challenge/response round trip first
    // (the existing 1W GET_NAME is unauthenticated).
    struct _p0x51 {
        char name[16];
    };

    // 0x52 SET_NAME (2W context): box -> device, 16-byte plain ASCII name
    // (same shape/byte layout as the 0x51 answer above, kept as its own
    // struct per this file's one-struct-per-command-byte convention).
    // Confirmed real bytes: TaHoma sending "Living Room Shut" (truncated at
    // 16 bytes) right after a successful first-time bond (Finding 27). No
    // confirmed challenge/response wrapper around it in that capture -
    // treated as best-effort/fire-and-forget, same as 0x6F SET_CONFIG1.
    struct _p0x52 {
        char name[16];
    };

    union _msg {
        _p0x01_13 p0x01_13;
        _p0x00_14 p0x00_14;
        _p0x20_15 p0x20_15;
        _p0x20_13 p0x20_13;
        _p0x00_16 p0x00_16;
        _p0x20_16 p0x20_16;
        _p0x2b p0x29;
        _p0x2b p0x2b;
        _p0x2e p0x2e;
        _p0x30 p0x30;
        _p0x2e p0x39; // same format of 2e
        _p0x1e p0x1e;
        _p0x29_resp p0x29_resp;
        _p0x6f p0x6f;
        _p0x32 p0x32;
        _p0x3c p0x3c;
        _p0x3d p0x3d;
        _p0x03 p0x03;
        _p0x04_14 p0x04_14;
        _p0x2a p0x2a;
        _p0x2e_2w p0x2e_2w;
        _p0x2f p0x2f;
        _p0x19 p0x19;
        _p0x51 p0x51;
        _p0x52 p0x52;
        _p0x00_2w p0x00_2w;
    };

    struct _packet {
        _header header;
        _msg msg;
    };

    typedef union {
        uint8_t buffer[MAX_FRAME_LEN];
        _packet packet;
    } Payload;

    /* keep the size of variable lenght of data */
    typedef struct {
        uint8_t memorizedCmd;
        std::vector<uint8_t> memorizedData;
    } Memorize;

    inline unsigned long packetStamp = 0L;
    inline unsigned long relStamp = 0L;
    inline size_t lastSendCmd = 0xFF;
    inline address lastFromAddress = {0};
    /**
    Class implementing the IOHC packet received/sent, including some additional members useful to track/control Radio parameters and timings
    */
    class iohcPacket {
    public:
        iohcPacket() = default;

        ~iohcPacket() = default;

        Payload payload{};
        uint8_t buffer_length = 0;
        uint32_t frequency = CHANNEL2; // Both 1W & 2W
        unsigned long repeatTime = 0L;
        uint8_t repeat = 0;
        bool lock = false;
        unsigned long delayed = 0;

        double afc{}; // AFC freq correction applied
        uint8_t snr{}; // in dB
        float rssi{}; // -RSSI*2 of last packet received
        uint8_t lna{}; // LNA attenuation in dB

        void decode(bool verbosity = false);
        std::string decodeToString(bool verbosity = false);

    protected:
        uint8_t source_originator[3] = {0};
    };
}
#endif
