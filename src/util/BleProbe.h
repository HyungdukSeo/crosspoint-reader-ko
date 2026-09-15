#pragma once

#ifdef CP_BLE_PROBE
#include <Arduino.h>

namespace BleProbe {
void command(const String& command);
bool poll();  // True on key input or while a diagnostic scan needs the device awake.
void stop();
void setUiOwnsInput(bool owns);
void reportUiInput();
}  // namespace BleProbe
#endif
