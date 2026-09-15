#pragma once

#ifdef CP_BLE_SCAN_DIAGNOSTICS
#include <Arduino.h>

namespace BleScanDiagnostics {
bool active();
bool command(const String& command);
bool poll();   // True while scanning, to keep the inactivity timer alive.
void reset();  // Only after BleHid.end() has stopped BLE callbacks.
}  // namespace BleScanDiagnostics
#endif
