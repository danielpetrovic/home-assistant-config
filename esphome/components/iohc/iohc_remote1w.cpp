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

#include "iohc_remote1w.h"
#include "iohcCryptoHelpers.h"
#include "esphome/core/log.h"
#include <esp_system.h>
#include <cstring>

namespace IOHC {

    static const char *const TAG = "iohc.remote1w";

    // ESP32 NVS namespace names are capped at 15 chars. Rather than require
    // every cover's YAML id to fit that, hash it down to a short, fixed,
    // deterministic namespace - collision risk is negligible for the handful
    // of covers a single bridge will ever manage.
    static std::string nvs_namespace_for(const std::string &pref_namespace) {
        size_t h = std::hash<std::string>{}(pref_namespace);
        char buf[11];
        snprintf(buf, sizeof(buf), "io%08x", static_cast<unsigned>(h));
        return std::string(buf);
    }

    void IOHCRemote1W::begin(iohcRadio *radio, const std::string &pref_namespace, const std::string &fixed_node_hex,
                              const std::string &fixed_key_hex) {
        radio_ = radio;
        pref_namespace_ = nvs_namespace_for(pref_namespace);
        has_fixed_identity_ = !fixed_node_hex.empty() && !fixed_key_hex.empty();
        if (has_fixed_identity_) {
            hexStringToBytes(fixed_node_hex, node_);
            hexStringToBytes(fixed_key_hex, key_);
        }
        load_or_generate_identity();
    }

    void IOHCRemote1W::load_or_generate_identity() {
        prefs_.begin(pref_namespace_.c_str(), false);

        if (has_fixed_identity_) {
            // node_/key_ already set directly from YAML/secrets.yaml in
            // begin() - only sequence/paired still need to persist. If this
            // is a replacement board, sequence resets to 1: the motor may
            // reject one stale-looking command before accepting the new
            // counter, but node/key (what actually determines bonding)
            // are unchanged, so no re-pairing is needed.
            sequence_ = prefs_.getUShort("seq", 1);
            paired_ = prefs_.getBool("paired", false);
            ESP_LOGCONFIG(TAG, "Using fixed identity %02X%02X%02X from YAML (seq=%u, paired=%s)", node_[0], node_[1],
                          node_[2], sequence_, paired_ ? "yes" : "no");
            return;
        }

        if (prefs_.isKey("node") && prefs_.getBytesLength("node") == sizeof(node_) &&
            prefs_.isKey("key") && prefs_.getBytesLength("key") == sizeof(key_)) {
            prefs_.getBytes("node", node_, sizeof(node_));
            prefs_.getBytes("key", key_, sizeof(key_));
            sequence_ = prefs_.getUShort("seq", 1);
            paired_ = prefs_.getBool("paired", false);
            ESP_LOGCONFIG(TAG, "Loaded bonded identity %02X%02X%02X (seq=%u, paired=%s)", node_[0], node_[1],
                          node_[2], sequence_, paired_ ? "yes" : "no");
            return;
        }

        // First boot for this cover: generate a fresh virtual remote identity,
        // same approach as upstream's addRemote() (random node address, random
        // key). Node uniqueness against other bonded remotes isn't checked
        // here since each cover has its own NVS namespace, not a shared list.
        for (uint8_t &b : node_) b = esp_random() & 0xff;
        for (uint8_t &b : key_) b = esp_random() & 0xff;
        sequence_ = 1;
        paired_ = false;

        prefs_.putBytes("node", node_, sizeof(node_));
        prefs_.putBytes("key", key_, sizeof(key_));
        prefs_.putUShort("seq", sequence_);
        prefs_.putBool("paired", paired_);
        ESP_LOGCONFIG(TAG, "Generated new virtual remote identity %02X%02X%02X", node_[0], node_[1], node_[2]);
    }

    void IOHCRemote1W::bump_and_persist_sequence() {
        sequence_ += 1;
        prefs_.putUShort("seq", sequence_);
    }

    // Ported near-verbatim from iohcRemote1W::forgePacket().
    void IOHCRemote1W::forge_packet(iohcPacket *packet) {
        IOHC::relStamp = esp_timer_get_time();

        packet->payload.packet.header.CtrlByte1.asStruct.MsgLen = sizeof(_header) - 1;
        packet->payload.packet.header.CtrlByte1.asStruct.Protocol = 1;
        packet->payload.packet.header.CtrlByte1.asStruct.StartFrame = 1;
        packet->payload.packet.header.CtrlByte1.asStruct.EndFrame = 1;
        packet->payload.packet.header.CtrlByte2.asByte = 0;
        packet->payload.packet.header.CtrlByte2.asStruct.LPM = 1;

        // Broadcast Target (by device type/group, not a specific unicast
        // device address - see the type_ comment in the header).
        uint16_t bcast = (static_cast<uint16_t>(type_) << 6) + 0b111111;
        packet->payload.packet.header.target[0] = 0x00;
        packet->payload.packet.header.target[1] = bcast >> 8;
        packet->payload.packet.header.target[2] = bcast & 0x00ff;

        for (size_t i = 0; i < sizeof(address); i++) packet->payload.packet.header.source[i] = node_[i];

        packet->frequency = CHANNEL2;
        packet->repeatTime = 40; // 40ms
        packet->repeat = 4;
        packet->lock = false;
    }

    void IOHCRemote1W::cmd(RemoteButton button, int percent) {
        position_tracker_.update();

        switch (button) {
            case RemoteButton::Prog:
                // Not a motor-side toggle (see the enum comment) - we decide
                // locally which real command to send, exactly like a real
                // remote would, from our own persisted paired_ flag.
                ESP_LOGI(TAG, "Prog pressed while paired_=%s -> sending %s", paired_ ? "true" : "false",
                         paired_ ? "Remove" : "Add");
                cmd(paired_ ? RemoteButton::Remove : RemoteButton::Add, percent);
                return;

            case RemoteButton::Pair:
            case RemoteButton::Remove: {
                // 0x2e (Pair) / 0x39 (Remove): HMAC-signed confirmation frame
                // (data + sequence[2] + hmac[6], 9 bytes), same payload shape
                // for both - only the cmd byte differs. Matches upstream
                // rspaargaren/iohomecontrol's own iohcRemote1W::cmd()
                // (src/iohcRemote1W.cpp, RemoteButton::Remove case). Velocet/
                // iown-homecontrol's commands.md documents a bare 2-byte
                // frame for 0x2e/0x39, but that's box/network-side traffic,
                // not what a 1W virtual remote sends.
                auto *packet = new iohcPacket;
                forge_packet(packet);
                packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x2e);
                packet->payload.packet.header.cmd = (button == RemoteButton::Pair) ? 0x2e : 0x39;

                packet->payload.packet.msg.p0x2e.data = 0x00;
                packet->payload.packet.msg.p0x2e.sequence[0] = sequence_ >> 8;
                packet->payload.packet.msg.p0x2e.sequence[1] = sequence_ & 0x00ff;
                bump_and_persist_sequence();

                uint8_t hmac[16];
                std::vector<uint8_t> frame(&packet->payload.packet.header.cmd, &packet->payload.packet.header.cmd + 2);
                iohcCrypto::create_1W_hmac(hmac, packet->payload.packet.msg.p0x2e.sequence, key_, frame);
                for (uint8_t i = 0; i < 6; i++) packet->payload.packet.msg.p0x2e.hmac[i] = hmac[i];

                packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;

                std::vector<iohcPacket *> packets2send{packet};
                // Explicit retune before every 1W send (Finding 29) - this
                // code has always assumed the radio's live frequency is
                // already CH2, which held true for this whole project until
                // the passive key-sniffer (Finding 28) started actively
                // hopping the RX frequency in the background. Without this,
                // a 1W send that happened to fire while the sniff had the
                // radio parked on CH1/CH3 would silently go out on the
                // wrong channel and never reach the motor - confirmed on
                // real hardware (a real-time Add ceremony failed silently
                // while the sniffer was armed).
                radio_->retune(CHANNEL2);
                radio_->send(packets2send);

                paired_ = (button == RemoteButton::Pair);
                prefs_.putBool("paired", paired_);
                ESP_LOGI(TAG, "%s sent to %02X%02X%02X", button == RemoteButton::Pair ? "Pair" : "Remove", node_[0],
                         node_[1], node_[2]);
                break;
            }

            case RemoteButton::Identify:
            case RemoteButton::StartIdentify:
            case RemoteButton::StopIdentify: {
                // 0x1E: identify/locate. Not documented in any 1W reference
                // (no physical remote has this button) - only known from a
                // real captured 2W TaHoma frame, which uses a bare 2-byte
                // payload with no HMAC (2W leans on its own live
                // challenge/response round trip for security instead). Since
                // a 1W remote has no round trip to lean on, this is modeled
                // as a self-signed frame (data[2]+sequence[2]+hmac[6]),
                // following the same pattern as Pair/Remove (0x2e/0x39) -
                // our own best-guess 1W-broadcast equivalent. Confirmed
                // working against real hardware. Payload bytes match the
                // real TaHoma capture exactly for all 3 variants.
                auto *packet = new iohcPacket;
                forge_packet(packet);
                packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x1e);
                packet->payload.packet.header.cmd = 0x1e;

                switch (button) {
                    case RemoteButton::Identify:
                        packet->payload.packet.msg.p0x1e.data[0] = 0x01;
                        packet->payload.packet.msg.p0x1e.data[1] = 0xff;
                        break;
                    case RemoteButton::StartIdentify:
                        packet->payload.packet.msg.p0x1e.data[0] = 0x01;
                        packet->payload.packet.msg.p0x1e.data[1] = 0x02;
                        break;
                    default: // StopIdentify
                        packet->payload.packet.msg.p0x1e.data[0] = 0x00;
                        packet->payload.packet.msg.p0x1e.data[1] = 0x00;
                        break;
                }

                packet->payload.packet.msg.p0x1e.sequence[0] = sequence_ >> 8;
                packet->payload.packet.msg.p0x1e.sequence[1] = sequence_ & 0x00ff;
                bump_and_persist_sequence();

                uint8_t hmac[16];
                std::vector<uint8_t> frame(&packet->payload.packet.header.cmd, &packet->payload.packet.header.cmd + 3);
                iohcCrypto::create_1W_hmac(hmac, packet->payload.packet.msg.p0x1e.sequence, key_, frame);
                for (uint8_t i = 0; i < 6; i++) packet->payload.packet.msg.p0x1e.hmac[i] = hmac[i];

                packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;

                std::vector<iohcPacket *> packets2send{packet};
                // Explicit retune before every 1W send (Finding 29) - this
                // code has always assumed the radio's live frequency is
                // already CH2, which held true for this whole project until
                // the passive key-sniffer (Finding 28) started actively
                // hopping the RX frequency in the background. Without this,
                // a 1W send that happened to fire while the sniff had the
                // radio parked on CH1/CH3 would silently go out on the
                // wrong channel and never reach the motor - confirmed on
                // real hardware (a real-time Add ceremony failed silently
                // while the sniffer was armed).
                radio_->retune(CHANNEL2);
                radio_->send(packets2send);

                const char *name = button == RemoteButton::Identify     ? "Identify"
                                    : button == RemoteButton::StartIdentify ? "Start Identify"
                                                                             : "Stop Identify";
                ESP_LOGI(TAG, "%s sent to %02X%02X%02X", name, node_[0], node_[1], node_[2]);
                break;
            }

            case RemoteButton::Add: {
                // 0x30: transmits our AES-encrypted key to the motor. Only
                // accepted while the motor is in its own physical
                // pairing/listen window.
                auto *packet = new iohcPacket;
                forge_packet(packet);
                packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x30);
                packet->payload.packet.header.cmd = 0x30;

                uint8_t enc_key[16];
                memcpy(enc_key, key_, 16);
                iohcCrypto::encrypt_1W_key(node_, enc_key);
                memcpy(packet->payload.packet.msg.p0x30.enc_key, enc_key, 16);

                packet->payload.packet.msg.p0x30.man_id = manufacturer_;
                packet->payload.packet.msg.p0x30.data = 0x01;
                packet->payload.packet.msg.p0x30.sequence[0] = sequence_ >> 8;
                packet->payload.packet.msg.p0x30.sequence[1] = sequence_ & 0x00ff;
                bump_and_persist_sequence();

                packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;

                std::vector<iohcPacket *> packets2send{packet};

                // EXPERIMENTAL, added 2026-07-10 to fix a real symptom: a
                // bare Add got the motor to jog (partial ack) but left it
                // still ignoring every subsequent movement command from us -
                // see io-2w-protocol.md. A real captured Situo Add ceremony
                // always follows Add with a burst of cmd=0x20 "private
                // write" frames (main=0x0c00 once, then main[0]=0x05 with
                // main[1] stepping 02,03,...,08, then a final 0xff sentinel)
                // and a closing cmd=0x2e Pair confirmation - we only ever
                // sent the bare Add. The _p0x20_13 struct shape is confirmed
                // against the real capture (the sequence field increments
                // exactly as expected across the burst), but the *meaning*
                // of main[0]/main[1] is not independently confirmed, only
                // mirrored from that one real capture - not proven on real
                // hardware until tested.
                auto send_private_write = [&](uint8_t m0, uint8_t m1) {
                    auto *pw = new iohcPacket;
                    forge_packet(pw);
                    pw->payload.packet.header.cmd = 0x20;
                    pw->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x20_13);

                    pw->payload.packet.msg.p0x20_13.origin = 0x02;
                    setAcei(pw->payload.packet.msg.p0x20_13.acei, 0x03);
                    pw->payload.packet.msg.p0x20_13.main[0] = m0;
                    pw->payload.packet.msg.p0x20_13.main[1] = m1;
                    pw->payload.packet.msg.p0x20_13.fp1 = 0x00;
                    pw->payload.packet.msg.p0x20_13.sequence[0] = sequence_ >> 8;
                    pw->payload.packet.msg.p0x20_13.sequence[1] = sequence_ & 0x00ff;
                    bump_and_persist_sequence();

                    uint8_t pw_hmac[16];
                    std::vector<uint8_t> pw_frame(&pw->payload.packet.header.cmd,
                                                   &pw->payload.packet.header.cmd + 6);
                    iohcCrypto::create_1W_hmac(pw_hmac, pw->payload.packet.msg.p0x20_13.sequence, key_, pw_frame);
                    for (uint8_t i = 0; i < 6; i++) pw->payload.packet.msg.p0x20_13.hmac[i] = pw_hmac[i];

                    pw->buffer_length = pw->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
                    packets2send.push_back(pw);
                };

                send_private_write(0x0c, 0x00);
                for (uint8_t step : {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xff}) {
                    send_private_write(0x05, step);
                }

                // Closing Pair (0x2e) confirmation - same frame shape as
                // RemoteButton::Pair above, sent here directly since it's
                // Add's own follow-through, not a separate user action.
                auto *pair = new iohcPacket;
                forge_packet(pair);
                pair->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x2e);
                pair->payload.packet.header.cmd = 0x2e;
                pair->payload.packet.msg.p0x2e.data = 0x00;
                pair->payload.packet.msg.p0x2e.sequence[0] = sequence_ >> 8;
                pair->payload.packet.msg.p0x2e.sequence[1] = sequence_ & 0x00ff;
                bump_and_persist_sequence();

                uint8_t pair_hmac[16];
                std::vector<uint8_t> pair_frame(&pair->payload.packet.header.cmd,
                                                 &pair->payload.packet.header.cmd + 2);
                iohcCrypto::create_1W_hmac(pair_hmac, pair->payload.packet.msg.p0x2e.sequence, key_, pair_frame);
                for (uint8_t i = 0; i < 6; i++) pair->payload.packet.msg.p0x2e.hmac[i] = pair_hmac[i];

                pair->buffer_length = pair->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
                packets2send.push_back(pair);

                // See Finding 29's comment above (other send sites in this
                // file) for why this explicit retune is required.
                radio_->retune(CHANNEL2);
                radio_->send(packets2send);

                paired_ = true;
                prefs_.putBool("paired", paired_);
                ESP_LOGI(TAG, "Add sent to %02X%02X%02X (full ceremony: Add + private-write + Pair, experimental)",
                         node_[0], node_[1], node_[2]);
                break;
            }

            case RemoteButton::Open:
            case RemoteButton::Close:
            case RemoteButton::Stop:
            case RemoteButton::Position:
            case RemoteButton::Vent: {
                // 0x00: Open/Close/Stop/Position/Vent all share this cmd byte -
                // the button identity is encoded in the "main" payload byte,
                // confirmed empirically against a real Situo remote: Open
                // main=0x00, Close main=0xc8, Stop main=0xd2. Vent (main=0xd8)
                // is what real TaHoma actually sends for "My" - confirmed via
                // a live capture of a real TaHoma "My" press, and matches
                // upstream's own RemoteButton::Vent case exactly. Stop
                // (0xd2) only has an effect while the motor is actively
                // moving; it is not the same command as "go to my/favorite
                // position from idle".
                auto *packet = new iohcPacket;
                forge_packet(packet);
                packet->payload.packet.header.cmd = 0x00;
                packet->payload.packet.msg.p0x00_14.origin = 0x01; // 0x01 = User
                setAcei(packet->payload.packet.msg.p0x00_14.acei, 0x43);

                // Companion cmd=0x20 frame fp1 value: real captures show the
                // Situo remote always follows the primary frame with a
                // second cmd=0x20 confirmation frame, identical except fp1 -
                // 0x00 for Open/Close, 0x02 for Stop/My. Both frames come
                // from a real device, so this is empirical, not guessed. Set
                // to -1 for commands we have no captured companion sample
                // for (Position), meaning no companion frame gets sent.
                int companion_fp1 = -1;

                switch (button) {
                    case RemoteButton::Open:
                        packet->payload.packet.msg.p0x00_14.main[0] = 0x00;
                        packet->payload.packet.msg.p0x00_14.main[1] = 0x00;
                        position_tracker_.startOpening();
                        companion_fp1 = 0x00;
                        break;
                    case RemoteButton::Close:
                        packet->payload.packet.msg.p0x00_14.main[0] = 0xc8;
                        packet->payload.packet.msg.p0x00_14.main[1] = 0x00;
                        position_tracker_.startClosing();
                        companion_fp1 = 0x00;
                        break;
                    case RemoteButton::Stop:
                        packet->payload.packet.msg.p0x00_14.main[0] = 0xd2;
                        packet->payload.packet.msg.p0x00_14.main[1] = 0x00;
                        position_tracker_.stop();
                        companion_fp1 = 0x02;
                        break;
                    case RemoteButton::Vent:
                        packet->payload.packet.msg.p0x00_14.main[0] = 0xd8;
                        // main[1]=0x00, NOT upstream's 0x03: a real captured
                        // TaHoma "My" frame (DATA 01 e7 d8 00 00 00) has
                        // main[1]=0x00, confirmed across two independent
                        // captures.
                        packet->payload.packet.msg.p0x00_14.main[1] = 0x00;
                        position_tracker_.stop();
                        // No real captured companion-frame sample for Vent
                        // (unlike Open/Close/Stop) - leaving companion_fp1 at
                        // -1 (no companion sent) until/unless a real capture
                        // says otherwise.
                        break;
                    case RemoteButton::Position: {
                        int p = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
                        // Matches upstream's (100-p)*2 formula: the motor
                        // reads main[0] on the same raw/2=percent scale its
                        // own status reports use. Self-consistent with the
                        // dedicated Open/Close commands above at the
                        // extremes: (100-100)*2=0, (100-0)*2=200=0xC8.
                        uint8_t val = static_cast<uint8_t>((100 - p) * 2);
                        packet->payload.packet.msg.p0x00_14.main[0] = val;
                        packet->payload.packet.msg.p0x00_14.main[1] = 0x00;
                        float current = position_tracker_.getPosition();
                        if (p > current + 0.5f)
                            position_tracker_.startOpening();
                        else if (p < current - 0.5f)
                            position_tracker_.startClosing();
                        else
                            position_tracker_.stop();
                        break;
                    }
                    default:
                        break;
                }

                packet->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x00_14);
                packet->payload.packet.msg.p0x00_14.sequence[0] = sequence_ >> 8;
                packet->payload.packet.msg.p0x00_14.sequence[1] = sequence_ & 0x00ff;
                bump_and_persist_sequence();

                uint8_t hmac[16];
                std::vector<uint8_t> frame(&packet->payload.packet.header.cmd, &packet->payload.packet.header.cmd + 7);
                iohcCrypto::create_1W_hmac(hmac, packet->payload.packet.msg.p0x00_14.sequence, key_, frame);
                for (uint8_t i = 0; i < 6; i++) packet->payload.packet.msg.p0x00_14.hmac[i] = hmac[i];

                packet->buffer_length = packet->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;

                std::vector<iohcPacket *> packets2send{packet};

                if (companion_fp1 >= 0) {
                    // cmd=0x20 confirmation frame the real remote always
                    // sends right after the primary one - see companion_fp1
                    // comment above for what's empirical vs assumed here.
                    auto *companion = new iohcPacket;
                    forge_packet(companion);
                    companion->payload.packet.header.cmd = 0x20;
                    companion->payload.packet.header.CtrlByte1.asStruct.MsgLen += sizeof(_p0x20_16);

                    companion->payload.packet.msg.p0x20_16.origin = 0x02;
                    setAcei(companion->payload.packet.msg.p0x20_16.acei, 0xff);
                    companion->payload.packet.msg.p0x20_16.main[0] = 0x01;
                    companion->payload.packet.msg.p0x20_16.main[1] = 0x43;
                    companion->payload.packet.msg.p0x20_16.fp1 = static_cast<uint8_t>(companion_fp1);
                    companion->payload.packet.msg.p0x20_16.fp2 = 0x0c;
                    companion->payload.packet.msg.p0x20_16.data[0] = 0x00;
                    companion->payload.packet.msg.p0x20_16.data[1] = 0x00;
                    companion->payload.packet.msg.p0x20_16.sequence[0] = sequence_ >> 8;
                    companion->payload.packet.msg.p0x20_16.sequence[1] = sequence_ & 0x00ff;
                    bump_and_persist_sequence();

                    uint8_t companion_hmac[16];
                    std::vector<uint8_t> companion_frame(&companion->payload.packet.header.cmd,
                                                          &companion->payload.packet.header.cmd + 9);
                    iohcCrypto::create_1W_hmac(companion_hmac, companion->payload.packet.msg.p0x20_16.sequence, key_,
                                                companion_frame);
                    for (uint8_t i = 0; i < 6; i++) companion->payload.packet.msg.p0x20_16.hmac[i] = companion_hmac[i];

                    companion->buffer_length = companion->payload.packet.header.CtrlByte1.asStruct.MsgLen + 1;
                    packets2send.push_back(companion);
                }

                // See Finding 29's comment above (other send sites in this
                // file) for why this explicit retune is required.
                radio_->retune(CHANNEL2);
                radio_->send(packets2send);

                ESP_LOGI(TAG, "Command sent to %02X%02X%02X, position now %.0f%%", node_[0], node_[1], node_[2],
                         position_tracker_.getPosition());
                break;
            }
        }
    }
}
