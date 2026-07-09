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

#include "iohc_blind_position.h"
#include <esp_timer.h>
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include "esphome/core/log.h"

namespace IOHC {
    // Shares the "iohc.cover" tag with iohc_cover.cpp - BlindPosition is only
    // ever owned/driven by a cover entity, never called from ISR context.
    static const char *const TAG = "iohc.cover";

    BlindPosition::BlindPosition(uint32_t travelTimeOpenSec, uint32_t travelTimeCloseSec)
            : state(State::Idle), travelTimeOpen(travelTimeOpenSec), travelTimeClose(travelTimeCloseSec),
              lastUpdateUs(0), position(0.0f) {}

    void BlindPosition::setTravelTimeOpen(uint32_t sec) { travelTimeOpen = sec; }

    void BlindPosition::setTravelTimeClose(uint32_t sec) { travelTimeClose = sec; }

    uint32_t BlindPosition::getTravelTimeOpen() const { return travelTimeOpen; }

    uint32_t BlindPosition::getTravelTimeClose() const { return travelTimeClose; }

    void BlindPosition::startOpening() {
        update();
        ESP_LOGV(TAG, "start opening (pos=%.1f%%)", position);
        state = State::Opening;
        lastUpdateUs = esp_timer_get_time();
    }

    void BlindPosition::startClosing() {
        update();
        ESP_LOGV(TAG, "start closing (pos=%.1f%%)", position);
        state = State::Closing;
        lastUpdateUs = esp_timer_get_time();
    }

    void BlindPosition::stop() {
        update();
        ESP_LOGV(TAG, "stop (pos=%.1f%%)", position);
        state = State::Idle;
    }

    void BlindPosition::update() {
    uint32_t travelTime = state == State::Closing ? travelTimeClose : travelTimeOpen;
    if (state == State::Idle || travelTime == 0) {
        lastUpdateUs = esp_timer_get_time();
        return;
    }

    uint64_t now = esp_timer_get_time();
    uint64_t elapsed = now - lastUpdateUs;
    float delta = static_cast<float>(elapsed) * 100.0f /
                  (static_cast<float>(travelTime) * 1000000.0f);

    if (state == State::Opening) {
        position += delta;
        if (position >= 99.5f) {  // margin to avoid floating point rounding issue
            position = 100.0f;
            state = State::Idle;
        }
    } else if (state == State::Closing) {
        position -= delta;
        if (position <= 0.5f) {  // margin for rounding
            position = 0.0f;
            state = State::Idle;
        }
    }

    // Clamp position to [0, 100]
    position = std::clamp(position, 0.0f, 100.0f);

    lastUpdateUs = now;
    // Only log on a whole-percent change to avoid per-tick spam.
    if (std::abs(position - lastLoggedPosition) >= 1.0f) {
        lastLoggedPosition = position;
        ESP_LOGV(TAG, "update (state=%d pos=%.0f%%)", static_cast<int>(state), position);
    }
}

    float BlindPosition::getPosition() const { return position; }

    bool BlindPosition::isMoving() const { return state != State::Idle; }

    void BlindPosition::setPosition(float pos) {
        position = std::clamp(pos, 0.0f, 100.0f);
        lastUpdateUs = esp_timer_get_time();
    }
}
