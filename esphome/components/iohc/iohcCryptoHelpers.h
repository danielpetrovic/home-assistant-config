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

// Was a Phase 0/1 stub (empty declarations, nothing called into it). Now a
// real port of upstream's iohcCryptoHelpers.h for Phase 2 pairing/command
// work. ESP32 path is pure mbedtls, no extra library dependency beyond what
// arduino-esp32 already bundles. Upstream's iohcCryptoHelpers.cpp also pulls
// in crypto2Wutils.h (2W challenge/response bonding key material), but none
// of its symbols are actually referenced by the functions declared here -
// that file is 2W-specific and out of scope until Phase 3, so it's
// deliberately not vendored.

#ifndef CRYPTO_H
#define CRYPTO_H

#include "iohc_board_config.h"
#include <string>
#include <vector>
#include <tuple>

#if defined(ESP8266)
    #include <Crypto.h>
    #include <AES.h>
    #include <CTR.h>
#elif defined(ESP32)
    #include "mbedtls/aes.h"        // AES functions
#endif

#define CRC_POLYNOMIAL_CCITT    0x8408

uint8_t hexStringToBytes(std::string hexString, uint8_t *byteString);
std::string bytesToHexString(const uint8_t *byteString, uint8_t len);

namespace iohcCrypto {
    uint16_t computeCrc(uint8_t data, uint16_t crc);
    uint16_t radioPacketComputeCrc(uint8_t *buffer, uint8_t bufferLength);
    uint16_t radioPacketComputeCrc(std::vector<uint8_t>& buffer);
    void encrypt_1W_key(const uint8_t *node_address, uint8_t *key);
    void create_1W_hmac(uint8_t *hmac, const uint8_t *seq_number, uint8_t *controller_key, const std::vector<uint8_t>& frame_data);

    // 2W bonding/control crypto (Phase 3b, corrected Finding 20). Built on
    // the existing constructInitialValue() `challenge` branch, same
    // primitive create_1W_hmac() already uses for its `sequence_number`
    // branch. Confirmed byte-for-byte against github.com/laberning/
    // home_io_control (a mature, independently-tested ESPHome IO-
    // Homecontrol component with confirmed real 2W pairing on the same
    // SX1276 chip family) - see io-2w-protocol.md Finding 20. That project's
    // proto_crypto.cpp construct_iv()/create_hmac()/crypt_key() match this
    // file's constructInitialValue()/AES-ECB-truncate-to-6 pattern exactly,
    // AND its TRANSFER_KEY constant is byte-identical to this file's
    // `transfer_key` - strong independent confirmation the primitives here
    // were always structurally right. What was wrong: BOTH functions below
    // used the fixed, public transfer_key as the ongoing authentication
    // key. The real protocol has a second, per-installation secret (a
    // 16-byte "system key", generated once by the controller/hub and never
    // transmitted in the clear) that gets used for every one of these
    // instead - transfer_key's only real job is to obfuscate that system
    // key during the one-time KEY_TRANSFERT (0x32) frame itself. This
    // exactly explains why testing these functions against real captures
    // using only transfer_key failed twice (addendum to Finding 6,
    // Finding 14).

    // Builds the 16-byte KEY_TRANSFERT (0x32) payload sent after a device's
    // CHALLENGE_REQUEST (0x3C) 6-byte challenge, during the key-exchange
    // phase of bonding (CMD_KEY_INIT 0x31 -> 0x3C -> this -> CMD_KEY_CONFIRM
    // 0x33). IV = constructInitialValue({0x3C, the CHALLENGE_REQUEST's own
    // cmd byte + its own 6-byte challenge}, challenge), AES-ECB encrypted
    // under the fixed, public transfer_key, then XORed with `system_key`.
    // frame_data corrected against a real, independently-verified working
    // reference (github.com/nicolas5000/io-rts-esp32's crypt_2w_key()) -
    // see io-2w-protocol.md Finding 28. Same XOR is symmetric, so this
    // function doubles as the passive-derivation primitive: called with a
    // captured KEY_TRANSFERT payload in place of a real system_key, it
    // recovers the real system_key instead (see IOHCController2W's passive
    // key-sniff state machine).
    void build_key_transfert_response(const uint8_t *motor_challenge, const uint8_t *system_key, uint8_t *out_response16);

    // Builds a 6-byte CHALLENGE_ANSWER (0x3D) payload in response to a
    // device's CHALLENGE_REQUEST (0x3C) 6-byte challenge, authenticating
    // whichever command was last sent (`last_sent_cmd` + `last_sent_data`).
    // IV = constructInitialValue({last_sent_cmd} + last_sent_data,
    // challenge), AES-ECB encrypted under `system_key` (the real shared
    // secret established at bonding, NOT transfer_key as the old,
    // unverified version of this function did), first 6 bytes of the
    // result - matches home_io_control's create_hmac()/create_challenge_resp().
    void build_challenge_answer(uint8_t last_sent_cmd, const std::vector<uint8_t>& last_sent_data, const uint8_t *challenge, const uint8_t *system_key, uint8_t *out_answer6);
}
#endif
