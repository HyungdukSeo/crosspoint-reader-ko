#pragma once

#ifdef CP_BLE_PROBE
#include <Arduino.h>

namespace BleProbe {
void command(const String& command);
bool poll();  // True when a key was received (resets the app inactivity timer).
void stop();
}  // namespace BleProbe
#endif
