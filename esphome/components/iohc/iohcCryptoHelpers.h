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

    // 2W bonding/control crypto (Phase 3b) - built on the existing
    // constructInitialValue() `challenge` branch, same primitive
    // create_1W_hmac() already uses for its `sequence_number` branch. Both
    // functions below are UNVERIFIED against a real KEY_TRANSFERT capture -
    // no genuine bonding ceremony has been observed yet on this install
    // (see io-2w-protocol.md Finding 14) - only derived from reading
    // upstream's exploratory main.cpp. The per-command CHALLENGE_ANSWER
    // shape (build_challenge_answer) is comparatively lower-risk: it
    // matches the structure of many real, confirmed 0x3C/0x3D exchanges
    // captured this session, just not independently verified to produce
    // byte-identical output to a real answer (that check, using only the
    // fixed transfer_key, already failed once - see the addendum to
    // Finding 6 - so treat this as one hypothesis for what a real per-motor
    // secret combines with, not as confirmed-correct).

    // Builds the 16-byte KEY_TRANSFERT (0x32) payload in response to a
    // motor's LAUNCH_KEY_TRANSFERT (0x38) 6-byte challenge. Per upstream's
    // RECEIVED_LAUNCH_KEY_TRANSFERT_0x38 handler: IV = constructInitialValue
    // ({0x31 /* SEND_ASK_CHALLENGE */}, challenge), encrypted under
    // transfer_key, then XORed with transfer_key again.
    void build_key_transfert_response(const uint8_t *motor_challenge, uint8_t *out_response16);

    // Builds a 6-byte CHALLENGE_ANSWER (0x3D) payload in response to a
    // device's CHALLENGE_REQUEST (0x3C) 6-byte challenge, authenticating
    // whichever command was last sent (`last_sent_cmd` + `last_sent_data`).
    // Per upstream's RECEIVED_CHALLENGE_REQUEST_0x3C handler's general
    // case (not the KEY_TRANSFERT-specific branch): IV =
    // constructInitialValue({last_sent_cmd} + last_sent_data, challenge),
    // encrypted under transfer_key, first 6 bytes of the result.
    void build_challenge_answer(uint8_t last_sent_cmd, const std::vector<uint8_t>& last_sent_data, const uint8_t *challenge, uint8_t *out_answer6);
}
#endif
