#pragma once

// Minimal stand-in for upstream's log_buffer.h (which backs an on-device web
// UI log view we're not vendoring for Phase 0/1). iohcRadio.cpp calls
// addLogMessage() unconditionally on every receive - route it to ESPHome's
// own logger instead of maintaining a separate buffer.

#include <Arduino.h>
#include <vector>

void addLogMessage(const String &msg);
std::vector<String> getLogMessages();
