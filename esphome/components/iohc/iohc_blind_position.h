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

// Renamed from upstream's "blind_position.h" (no iohc_ prefix upstream) to
// match this component's flat-directory naming convention - see iohc.h.
//
// Diverges from upstream: split the single travelTime into separate
// open/close values. Real motors commonly move at different speeds in each
// direction (gravity helps closing, doesn't help opening), and 230V/high-power
// motor types move faster than battery ones - a single shared value can't
// represent that. Not user-configurable (see IOHCCover::TRAVEL_TIME_OPEN/
// CLOSE) - this estimate is purely cosmetic, so a fixed value is enough.

#ifndef IOHC_BLIND_POSITION_H
#define IOHC_BLIND_POSITION_H

#include <stdint.h>

namespace IOHC {
    class BlindPosition {
    public:
        explicit BlindPosition(uint32_t travelTimeOpenSec = 0, uint32_t travelTimeCloseSec = 0);

        void setTravelTimeOpen(uint32_t sec);
        void setTravelTimeClose(uint32_t sec);
        uint32_t getTravelTimeOpen() const;
        uint32_t getTravelTimeClose() const;

        void startOpening();
        void startClosing();
        void stop();
        void update();

        float getPosition() const;
        bool isMoving() const;
        void setPosition(float pos);

    private:
        enum class State { Idle, Opening, Closing };
        State state;
        uint32_t travelTimeOpen;  // seconds
        uint32_t travelTimeClose; // seconds
        uint64_t lastUpdateUs;
        float position; // 0..100
        float lastLoggedPosition{-1.0f}; // last value logged; -1 = never logged
    };
}

#endif // IOHC_BLIND_POSITION_H
