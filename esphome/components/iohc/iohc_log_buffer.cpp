#include "iohc_log_buffer.h"
#include "esphome/core/log.h"

static const char *const TAG = "iohc.upstream";

void addLogMessage(const String &msg) { ESP_LOGD(TAG, "%s", msg.c_str()); }

std::vector<String> getLogMessages() { return {}; }
