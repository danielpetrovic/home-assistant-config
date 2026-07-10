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

#include "iohcPacket.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <esp_attr.h>
#include "esp_log.h"
#include "iohc_utils.h"
#include <sstream>
#include <iomanip>

namespace IOHC {
    static const char *const TAG = "iohc.packet";

    void IRAM_ATTR iohcPacket::decode(bool verbosity) {
        // Buffer the whole decoded line instead of printf-ing each field
        // separately, so it can be gated behind `verbosity` and emitted as
        // one properly-tagged log entry per packet.
        char logbuf[1024];
        size_t logbuf_len = 0;
        auto appendf = [&](const char *fmt, ...) {
            if (logbuf_len >= sizeof(logbuf) - 1) return;
            va_list args;
            va_start(args, fmt);
            int n = vsnprintf(logbuf + logbuf_len, sizeof(logbuf) - logbuf_len, fmt, args);
            va_end(args);
            if (n > 0) logbuf_len += n;
        };

        if (packetStamp - relStamp > 500000L) {
            relStamp = packetStamp; // - this->relStamp;
            // for (uint8_t i = 0; i < 3; i++)
            //     source_originator[i] = this->payload.packet.header.source[i];
        }
        char _dir[3] = {};
        if (!memcmp(source_originator, this->payload.packet.header.source, 3))
            _dir[0] = '>';
        else
            _dir[0] = '<';

if(this->payload.packet.header.CtrlByte1.asStruct.Protocol) _dir[0] = '>';
else if (this->payload.packet.header.CtrlByte1.asStruct.StartFrame && !this->payload.packet.header.CtrlByte1.asStruct.EndFrame) _dir[0] = '>';
else if (!this->payload.packet.header.CtrlByte1.asStruct.StartFrame && this->payload.packet.header.CtrlByte1.asStruct.EndFrame) _dir[0] = '<';
else _dir[0] = ' ';

        appendf("(%2.2u) %1xW S %s E %s ", this->payload.packet.header.CtrlByte1.asStruct.MsgLen,
               this->payload.packet.header.CtrlByte1.asStruct.Protocol ? 1 : 2,
               this->payload.packet.header.CtrlByte1.asStruct.StartFrame ? "1" : "0",
               this->payload.packet.header.CtrlByte1.asStruct.EndFrame ? "1" : "0");

        if (this->payload.packet.header.CtrlByte2.asStruct.LPM) appendf("[LPM]");
        if (this->payload.packet.header.CtrlByte2.asStruct.Beacon) appendf("[B]");
        if (this->payload.packet.header.CtrlByte2.asStruct.Routed) appendf("[R]");
        if (this->payload.packet.header.CtrlByte2.asStruct.Prio) appendf("[PRIO]");
        if (this->payload.packet.header.CtrlByte2.asStruct.Unk2) appendf("[U2]");
        if (this->payload.packet.header.CtrlByte2.asStruct.Unk3) appendf("[U3]");
        if (this->payload.packet.header.CtrlByte2.asStruct.Version)
            appendf( "[V]%u", this->payload.packet.header.CtrlByte2.asStruct.Version);

        //            const char *commandName = commands[msg_cmd_id].c_str();
        //            Serial.print(commandName);

        appendf("\tFROM %2.2X%2.2X%2.2X TO %2.2X%2.2X%2.2X CMD %2.2X",
               this->payload.packet.header.source[0], this->payload.packet.header.source[1],
               this->payload.packet.header.source[2],
               this->payload.packet.header.target[0], this->payload.packet.header.target[1],
               this->payload.packet.header.target[2],
               this->payload.packet.header.cmd);

        // if (verbosity) appendf(" +%03.3f F%03.3f, %03.1fdBm %f %f\t", static_cast<float>(packetStamp - relStamp)/1000.0, static_cast<float>(this->frequency)/1000000.0, this->rssi, this->lna, this->afc);
        // if (verbosity) appendf(" +%03.3f F%03.3f, %03.1fdBm %f\t", static_cast<float>(packetStamp - relStamp)/1000.0, static_cast<float>(this->frequency)/1000000.0, this->rssi, this->afc);
        // if (verbosity) appendf(" +%03.3f\t%03.1fdBm\t", static_cast<float>(packetStamp - relStamp)/1000.0,  this->rssi);
//        if (verbosity) appendf(" +%03.3f F%03.3f\t", static_cast<float>(packetStamp - relStamp)/1000.0, static_cast<float>(this->frequency)/1000000.0);
        if (verbosity) appendf(" +%03.3f\t", static_cast<float>(packetStamp - relStamp)/1000.0);
        appendf(" %s ", _dir);

        uint8_t dataLen = this->buffer_length - 9;
        appendf(" DATA(%2.2u) ", dataLen);

        // 1W fields
        if (this->payload.packet.header.CtrlByte1.asStruct.Protocol) {
            unsigned data_length = dataLen - 8;

            std::string msg_data = bitrow_to_hex_string(this->payload.buffer + 9, dataLen/*data_length*/);
            appendf(" %s", msg_data.c_str());

            switch (this->payload.packet.header.cmd) {
                case 0x30: {
                    appendf("\tMANU %X DATA %X ", this->payload.packet.msg.p0x30.man_id, this->payload.packet.msg.p0x30.data);
                    appendf("\tKEY %s SEQ %s ", bitrow_to_hex_string(this->payload.packet.msg.p0x30.enc_key, 16).c_str(),
                           bitrow_to_hex_string(this->payload.packet.msg.p0x30.sequence, 2).c_str());
                    break;
                }
                case 0x2E:
                case 0x39: {
                    appendf("\tDATA %X ", this->payload.packet.msg.p0x2e.data);
                    appendf("\tSEQ %s MAC %s ", bitrow_to_hex_string(this->payload.packet.msg.p0x2e.sequence, 2).c_str(),
                           bitrow_to_hex_string(this->payload.packet.msg.p0x2e.hmac, 6).c_str());
                    break;
                }
                case 0x20: {
                    if (dataLen == 13) {
                        appendf("\tSEQ %s MAC %s ",
                               bitrow_to_hex_string(this->payload.packet.msg.p0x20_13.sequence, 2).c_str(),
                               bitrow_to_hex_string(this->payload.packet.msg.p0x20_13.hmac, 6).c_str());
                        auto main = static_cast<unsigned>((this->payload.packet.msg.p0x20_13.main[0] << 8) | this->payload.packet.msg.p0x20_13.main[1]);
                        appendf(" Manuf %X Acei %X Main %X fp1 %X ", this->payload.packet.msg.p0x20_13.origin,
                               this->payload.packet.msg.p0x20_13.acei.asByte, main,
                               this->payload.packet.msg.p0x20_13.fp1
                               );

                        // auto acei = this->payload.packet.msg.p0x20_13.acei;
                        // appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                    }
                    if (dataLen == 15) {
                        appendf("\tSEQ %s MAC %s ",
                               bitrow_to_hex_string(this->payload.packet.msg.p0x20_15.sequence, 2).c_str(),
                               bitrow_to_hex_string(this->payload.packet.msg.p0x20_15.hmac, 6).c_str());
                        auto main = static_cast<unsigned>(  (this->payload.packet.msg.p0x20_15.main[0] << 8) | this->payload.packet.msg.p0x20_15.main[1]);
                        appendf(" Manuf %X Acei %X Main %X fp1 %X fp2 %X fp3 %X ", this->payload.packet.msg.p0x20_15.origin,
                               this->payload.packet.msg.p0x20_15.acei.asByte, main,
                               this->payload.packet.msg.p0x20_15.fp1,
                               this->payload.packet.msg.p0x20_15.fp2,
                               this->payload.packet.msg.p0x20_15.fp3);

                        // auto acei = this->payload.packet.msg.p0x20_15.acei;
                        // appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                    }
                                        if (dataLen == 16) {
                        appendf("\tSEQ %s MAC %s ",
                               bitrow_to_hex_string(this->payload.packet.msg.p0x20_16.sequence, 2).c_str(),
                               bitrow_to_hex_string(this->payload.packet.msg.p0x20_16.hmac, 6).c_str());
                        auto main = static_cast<unsigned>((this->payload.packet.msg.p0x20_16.main[0] << 8) | this->payload.packet.msg.p0x20_16.main[1]);
                        auto data = static_cast<unsigned>((this->payload.packet.msg.p0x20_16.data[0] << 8) | this->payload.packet.msg.p0x20_16.data[1]);
                        appendf(" Manu %X Acei %X Main %4X fp1 %X fp2 %X Data %4X", this->payload.packet.msg.p0x20_16.origin,
                               this->payload.packet.msg.p0x20_16.acei.asByte, main,
                               this->payload.packet.msg.p0x20_16.fp1,
                               this->payload.packet.msg.p0x20_16.fp2, data);

                        // auto acei = this->payload.packet.msg.p0x20_16.acei;
                        // appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                    }

                    break;
                }
                case 0x28:
                case 0x01:
                case 0x00: {
                    // auto main = static_cast<unsigned>((this->payload.packet.msg.p0x00.main[0] << 8) | this->payload.packet.msg.p0x00.main[1]);
                    // appendf("Org %X Acei %X Main %X fp1 %X fp2 %X ", this->payload.packet.msg.p0x00.origin, this->payload.packet.msg.p0x00.acei, main, this->payload.packet.msg.p0x00.fp1, this->payload.packet.msg.p0x00.fp2);
                    // appendf("tSEQ %s Hmac %s", bitrow_to_hex_string(this->payload.packet.msg.p0x01_13.sequence, 2).c_str(), bitrow_to_hex_string(this->payload.packet.msg.p0x01_13.hmac, 6).c_str());
                    //                    int msg_seq_nr = 0;
                    //                    msg_seq_nr = (this->payload.buffer[9 + data_length] << 8) | this->payload.buffer[9 + data_length/* + 1*/];
                    //                    appendf("\tSEQ %3.2X", msg_seq_nr);
                    //                    std::string msg_mac = bitrow_to_hex_string(this->payload.buffer + 9 + data_length + 2, 6);
                    //                    appendf(" MAC %s ", msg_mac.c_str());

                    if (dataLen == 13) {
                        appendf("\tSEQ %s MAC %s ",
                               bitrow_to_hex_string(this->payload.packet.msg.p0x01_13.sequence, 2).c_str(),
                               bitrow_to_hex_string(this->payload.packet.msg.p0x01_13.hmac, 6).c_str());
                        auto main = static_cast<unsigned>((this->payload.packet.msg.p0x01_13.main) /*[0] << 8) | this->payload.packet.msg.p0x01_13.main[1]*/);
                        appendf(" Org %X Acei %X Main %X fp1 %X fp2 %X ", this->payload.packet.msg.p0x01_13.origin,
                               this->payload.packet.msg.p0x01_13.acei.asByte, main,
                               this->payload.packet.msg.p0x01_13.fp1,
                               this->payload.packet.msg.p0x01_13.fp2);

                        auto acei = this->payload.packet.msg.p0x01_13.acei;
                        appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                    }
                    if (dataLen == 14) {
                        appendf("\tSEQ %s MAC %s ",
                               bitrow_to_hex_string(this->payload.packet.msg.p0x00_14.sequence, 2).c_str(),
                               bitrow_to_hex_string(this->payload.packet.msg.p0x00_14.hmac, 6).c_str());
                        auto main = static_cast<unsigned>((this->payload.packet.msg.p0x00_14.main[0] << 8) /* | this->payload.packet.msg.p0x00_14.main[1]*/);
                        appendf(" Org %X Acei %X Main %X fp1 %X fp2 %X ", this->payload.packet.msg.p0x00_14.origin,
                               this->payload.packet.msg.p0x00_14.acei.asByte, main,
                               this->payload.packet.msg.p0x00_14.fp1,
                               this->payload.packet.msg.p0x00_14.fp2);

                        auto acei = this->payload.packet.msg.p0x00_14.acei;
                        appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                    }
                    if (dataLen == 16) {
                        appendf("\tSEQ %s MAC %s ",
                               bitrow_to_hex_string(this->payload.packet.msg.p0x00_16.sequence, 2).c_str(),
                               bitrow_to_hex_string(this->payload.packet.msg.p0x00_16.hmac, 6).c_str());
                        auto main = static_cast<unsigned>((this->payload.packet.msg.p0x00_16.main[0] << 8) | this->payload.packet.msg.p0x00_16.main[1]);
                        auto data = static_cast<unsigned>((this->payload.packet.msg.p0x00_16.data[0] << 8) | this->payload.packet.msg.p0x00_16.data[1]);
                        appendf(" Org %X Acei %X Main %4X fp1 %X fp2 %X Data %4X", this->payload.packet.msg.p0x00_16.origin,
                               this->payload.packet.msg.p0x00_16.acei.asByte, main,
                               this->payload.packet.msg.p0x00_16.fp1,
                               this->payload.packet.msg.p0x00_16.fp2, data);

                        auto acei = this->payload.packet.msg.p0x00_16.acei;
                        appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                    }
                    break;
                }
                default: {
                    // std::string msg_data = bitrow_to_hex_string(this->payload.buffer + 9, data_length);
                    // appendf(" %s", msg_data.c_str());

                    // int msg_seq_nr = 0;
                    // msg_seq_nr = (this->payload.buffer[9 + data_length] << 8) | this->payload.buffer[9 + data_length + 1];
                    // appendf("\tSEQ %3.2X", msg_seq_nr);

                    // std::string msg_mac = bitrow_to_hex_string(this->payload.buffer + 9 + data_length + 2, 6);
                    // appendf(" MAC %s ", msg_mac.c_str());

                    // appendf("\tSEQ %s MAC %s ", bitrow_to_hex_string(this->payload.packet.msg.p0x00.sequence, 2).c_str(), bitrow_to_hex_string(this->payload.packet.msg.p0x00.hmac, 6).c_str());
                }
            }
            uint16_t broadcast = ((this->payload.packet.header.target[1]) << 2) | ( (this->payload.packet.header.target[2] >> 6) & 0x03);
            const char* typeName = sDevicesType[broadcast].c_str();
            appendf(" Type %s ", typeName);
        }
        // 2W fields
        else {
            // Human-readable command name for the bonding-family commands,
            // purely for log readability during Phase 3a's capture work -
            // see iohcPacket.h's "2W bonding-family structs" comment block
            // for the unverified-hypothesis caveat that applies to all of
            // these until confirmed against a real motor.
            const char *cmd2w_name = nullptr;
            switch (this->payload.packet.header.cmd) {
                case 0x28: cmd2w_name = "DISCOVER"; break;
                case 0x29: cmd2w_name = "DISCOVER_ANSWER"; break;
                case 0x2A: cmd2w_name = "DISCOVER_REMOTE"; break;
                case 0x2B: cmd2w_name = "DISCOVER_REMOTE_ANSWER"; break;
                case 0x2C: cmd2w_name = "DISCOVER_ACTUATOR"; break;
                case 0x2D: cmd2w_name = "DISCOVER_ACTUATOR_ACK"; break;
                case 0x31: cmd2w_name = "ASK_CHALLENGE"; break;
                case 0x32: cmd2w_name = "KEY_TRANSFERT"; break;
                case 0x33: cmd2w_name = "KEY_TRANSFERT_ACK"; break;
                case 0x36: cmd2w_name = "ADDRESS_REQUEST"; break;
                case 0x38: cmd2w_name = "LAUNCH_KEY_TRANSFERT"; break;
                case 0x3C: cmd2w_name = "CHALLENGE_REQUEST"; break;
                case 0x3D: cmd2w_name = "CHALLENGE_ANSWER"; break;
                case 0x03: cmd2w_name = "PRIVATE_COMMAND"; break;
                case 0x04: cmd2w_name = "PRIVATE_COMMAND_ANSWER"; break;
                case 0x19: cmd2w_name = "PRIVATE_UNKNOWN_0x19"; break;
                case 0x2E: cmd2w_name = "DEVICE_CONFIRM"; break;
                case 0x2F: cmd2w_name = "DEVICE_CONFIRM_ACK"; break;
                case 0x50: cmd2w_name = "GET_NAME"; break;
                case 0x51: cmd2w_name = "NAME_ANSWER"; break;
                case 0xFE: cmd2w_name = "STATUS"; break;
                default: break;
            }
            if (cmd2w_name != nullptr) appendf(" [%s]", cmd2w_name);

            if (dataLen != 0) {
                std::string msg_data = bitrow_to_hex_string(this->payload.buffer + 9, dataLen);
                appendf(" %s", msg_data.c_str());
                if (this->payload.packet.header.cmd == 0x00 || this->payload.packet.header.cmd == 0x01) {
                    auto main = static_cast<unsigned>((this->payload.packet.msg.p0x01_13.main) /*[0] << 8) | this->payload.packet.msg.p0x01_13.main[1]*/);
                    appendf(" Org %X Acei %X Main %X fp1 %X fp2 %X ", this->payload.packet.msg.p0x01_13.origin,
                           this->payload.packet.msg.p0x01_13.acei.asByte, main, this->payload.packet.msg.p0x01_13.fp1,
                           this->payload.packet.msg.p0x01_13.fp2);

                    auto acei = this->payload.packet.msg.p0x01_13.acei;
                    appendf(" Acei %u %u %u %u ", acei.asStruct.level, acei.asStruct.service, acei.asStruct.extended, acei.asStruct.isvalid);
                }
                /*Private Atlantic/Sauter/Thermor*/
                if (this->payload.packet.header.cmd == 0x20) {}

                // 2W bonding-family field breakdown - struct-based only
                // where dataLen matches the hypothesized fixed size from
                // iohcPacket.h; otherwise the raw hex dump above is all
                // that's printed; deliberately not a `default:` fallback,
                // since a length mismatch is itself useful capture signal
                // (confirms or refutes the hypothesized struct sizes).
                switch (this->payload.packet.header.cmd) {
                    case 0x29:
                        if (dataLen == 9)
                            appendf(" Gateway %s Manuf %X Info %X",
                                   bitrow_to_hex_string(this->payload.packet.msg.p0x29_ack.gateway, 3).c_str(),
                                   this->payload.packet.msg.p0x29_ack.manufacturer,
                                   this->payload.packet.msg.p0x29_ack.info);
                        break;
                    case 0x38:
                        if (dataLen == 6)
                            appendf(" Challenge %s", bitrow_to_hex_string(this->payload.packet.msg.p0x38.challenge, 6).c_str());
                        break;
                    case 0x32:
                        if (dataLen == 16)
                            appendf(" EncKey %s", bitrow_to_hex_string(this->payload.packet.msg.p0x32.encrypted_key, 16).c_str());
                        break;
                    case 0x3C:
                        if (dataLen == 6)
                            appendf(" Challenge %s", bitrow_to_hex_string(this->payload.packet.msg.p0x3c.challenge, 6).c_str());
                        break;
                    case 0x3D:
                        if (dataLen == 6)
                            appendf(" Response %s", bitrow_to_hex_string(this->payload.packet.msg.p0x3d.response, 6).c_str());
                        // dataLen==16 case (bonding-time key echo) intentionally
                        // left as raw hex only - see iohcPacket.h struct comment.
                        break;
                    case 0x03:
                        if (dataLen == 3)
                            appendf(" Query %s", bitrow_to_hex_string(this->payload.packet.msg.p0x03.data, 3).c_str());
                        break;
                    case 0x04:
                        if (dataLen == 14) {
                            auto main = static_cast<unsigned>((this->payload.packet.msg.p0x04_14.main[0] << 8) | this->payload.packet.msg.p0x04_14.main[1]);
                            appendf(" Status %X Unk1 %X Main %4X (=%.0f%% closed) Commander %s",
                                   this->payload.packet.msg.p0x04_14.status,
                                   this->payload.packet.msg.p0x04_14.unknown1,
                                   main, main / 512.0f,
                                   bitrow_to_hex_string(this->payload.packet.msg.p0x04_14.commanding_controller, 3).c_str());
                        }
                        break;
                    case 0x2A:
                        if (dataLen == 12)
                            appendf(" Data %s", bitrow_to_hex_string(this->payload.packet.msg.p0x2a.data, 12).c_str());
                        break;
                    case 0x2E:
                        if (dataLen == 1)
                            appendf(" Data %X", this->payload.packet.msg.p0x2e_2w.data);
                        break;
                    case 0x2F:
                        if (dataLen == 1)
                            appendf(" Data %X", this->payload.packet.msg.p0x2f.data);
                        break;
                    case 0x19:
                        if (dataLen == 1)
                            appendf(" Data %X", this->payload.packet.msg.p0x19.data);
                        break;
                    case 0x51:
                        if (dataLen == 16)
                            appendf(" Name \"%.16s\"", this->payload.packet.msg.p0x51.name);
                        break;
                    default:
                        break;
                }
            }
        }

        if (verbosity) ESP_LOGV(TAG, "%s", logbuf);

        relStamp = packetStamp;
    }

    std::string iohcPacket::decodeToString(bool verbosity) {
        std::ostringstream ss;
        char dir = ' ';
        if (!memcmp(source_originator, this->payload.packet.header.source, 3))
            dir = '>';
        else
            dir = '<';
        if(this->payload.packet.header.CtrlByte1.asStruct.Protocol) dir = '>';
        else if (this->payload.packet.header.CtrlByte1.asStruct.StartFrame && !this->payload.packet.header.CtrlByte1.asStruct.EndFrame) dir = '>';
        else if (!this->payload.packet.header.CtrlByte1.asStruct.StartFrame && this->payload.packet.header.CtrlByte1.asStruct.EndFrame) dir = '<';

        ss << "(" << std::setw(2) << std::setfill('0') << std::dec
           << (int)this->payload.packet.header.CtrlByte1.asStruct.MsgLen << ") ";
        ss << (this->payload.packet.header.CtrlByte1.asStruct.Protocol ? "1W" : "2W") << " ";
        ss << "FROM " << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << (int)this->payload.packet.header.source[0]
           << (int)this->payload.packet.header.source[1]
           << (int)this->payload.packet.header.source[2]
           << " TO "
           << (int)this->payload.packet.header.target[0]
           << (int)this->payload.packet.header.target[1]
           << (int)this->payload.packet.header.target[2]
           << " CMD " << (int)this->payload.packet.header.cmd;

        uint8_t dataLen = this->buffer_length - 9;
        ss << " DATA(" << std::dec << (int)dataLen << ") ";
        if (dataLen)
            ss << bitrow_to_hex_string(this->payload.buffer + 9, dataLen);
        ss << " " << dir;
        return ss.str();
    }
}
