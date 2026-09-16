#include "BleScanDiagnostics.h"

#ifdef CP_BLE_SCAN_DIAGNOSTICS
#include <BleKeyboardHost.h>
#include <Logging.h>
#include <NimBLEDevice.h>

#include <cstring>

namespace BleScanDiagnostics {
namespace {
// Count all callbacks, but retain only the latest target observation.
// Serial output runs once per second outside the NimBLE host task.
struct Record {
  char address[18]{};
  char name[32]{};
  char payload[33]{};  // First 16 payload bytes, hex encoded.
  int16_t rssi = 0;
  uint16_t bytes = 0;
  uint8_t addressType = 0;
  uint8_t primary = 0;
  uint8_t secondary = 0;
  uint8_t dataStatus = 0;
  bool discovered = false;
  bool hid = false;
  bool connectable = false;
  bool legacy = false;
};
Record latestTarget;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
unsigned discoveries = 0, results = 0, targetDiscoveries = 0, targetResults = 0;
unsigned long lastReport = 0;
bool mode = false;
bool ended = false;
int endReason = 0;

class Callbacks : public NimBLEScanCallbacks {
  static void capture(const NimBLEAdvertisedDevice* device, bool discovered) {
    if (!device) return;
    const auto address = device->getAddress();
    const auto addr = address.toString();
    const auto name = device->getName();
    const bool target = addr == "d8:c3:e4:63:4d:ab" || name.find("Power Keyboard") != std::string::npos;
    portENTER_CRITICAL(&mux);
    if (discovered)
      ++discoveries;
    else
      ++results;
    if (target) {
      if (discovered)
        ++targetDiscoveries;
      else
        ++targetResults;
    }
    portEXIT_CRITICAL(&mux);
    if (!target) return;
    Record record;
    snprintf(record.address, sizeof(record.address), "%s", addr.c_str());
    snprintf(record.name, sizeof(record.name), "%s", name.c_str());
    record.addressType = address.getType();
    record.rssi = device->getRSSI();
    record.hid = device->isAdvertisingService(NimBLEUUID(uint16_t{0x1812}));
    record.connectable = device->isConnectable();
#if CONFIG_BT_NIMBLE_EXT_ADV
    record.legacy = device->isLegacyAdvertisement();
    record.primary = device->getPrimaryPhy();
    record.secondary = device->getSecondaryPhy();
    record.dataStatus = device->getDataStatus();
#else
    record.legacy = true;
    record.primary = 1;
#endif
    record.discovered = discovered;
    const auto& payload = device->getPayload();
    record.bytes = payload.size();
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < payload.size() && i < 16; ++i) {
      record.payload[i * 2] = hex[payload[i] >> 4];
      record.payload[i * 2 + 1] = hex[payload[i] & 15];
    }
    portENTER_CRITICAL(&mux);
    latestTarget = record;
    portEXIT_CRITICAL(&mux);
  }
  void onDiscovered(const NimBLEAdvertisedDevice* device) override { capture(device, true); }
  void onResult(const NimBLEAdvertisedDevice* device) override { capture(device, false); }
  void onScanEnd(const NimBLEScanResults&, int reason) override {
    portENTER_CRITICAL(&mux);
    ended = true;
    endReason = reason;
    portEXIT_CRITICAL(&mux);
  }
} callbacks;
}  // namespace

bool active() { return mode; }

void reset() {
  mode = false;
  portENTER_CRITICAL(&mux);
  discoveries = results = targetDiscoveries = targetResults = 0;
  latestTarget = {};
  lastReport = millis();
  ended = false;
  portEXIT_CRITICAL(&mux);
}

bool command(const String& command) {
  if (!command.startsWith("BLE:RAW")) return false;
  String scanCommand = command;
  const uint16_t interval = command.endsWith(":160") ? 160 : 30;
  if (interval == 160) scanCommand.remove(scanCommand.length() - 4);
  const bool passive = scanCommand == "BLE:RAW:PASSIVE";
  const bool oneM = scanCommand == "BLE:RAW:1M";
  if (!passive && !oneM && scanCommand != "BLE:RAW:ACTIVE") {
    LOG_INF("SCAN", "Use CMD:BLE:RAW:ACTIVE, PASSIVE or 1M; append :160 for baseline");
    return true;
  }
  if (!BleHid.isRunning() || BleHid.isConnecting() || BleHid.isConnected()) {
    LOG_INF("SCAN",
            "Requires BLE ON with no connection or connection attempt; switch keyboard off during ON if bonded");
    return true;
  }
  auto* scan = NimBLEDevice::getScan();
  if (mode) {
    LOG_INF("SCAN", "Run CMD:BLE:OFF then ON before another raw scan");
    return true;
  }
  BleHid.stopScan();
  reset();
  mode = true;  // Suppress SDK auto-reconnect while these callbacks own the scan.
  scan->clearResults();
  scan->setScanCallbacks(&callbacks, true);
  scan->setActiveScan(!passive);
  scan->setDuplicateFilter(0);
  scan->setFilterPolicy(0);
  scan->setLimitedOnly(false);
  scan->setInterval(interval);
  scan->setWindow(interval);
#if CONFIG_BT_NIMBLE_EXT_ADV
  scan->setPhy(oneM ? NimBLEScan::SCAN_1M : NimBLEScan::SCAN_ALL);
#endif
  LOG_INF("SCAN", "extendedAdvertising=%d", CONFIG_BT_NIMBLE_EXT_ADV);
  // Callback-only mode bounds NimBLE's retained scan results; our target snapshot is
  // fixed-size. Pending active-scan responses may still need temporary storage.
  scan->setMaxResults(0);
  LOG_INF("SCAN", "before interval/window=%u free=%u maxBlock=%u", interval, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const bool started = scan->start(15000, false, true);
  LOG_INF("SCAN", "mode=%s start=%d scanning=%d duration=15000ms free=%u maxBlock=%u", command.c_str(), started,
          scan->isScanning(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

bool poll() {
  const auto now = millis();
  portENTER_CRITICAL(&mux);
  const bool finished = ended;
  const auto disc = discoveries, result = results;
  const auto targetDisc = targetDiscoveries, targetResult = targetResults;
  const auto target = latestTarget;
  const int reason = endReason;
  if (finished) ended = false;
  portEXIT_CRITICAL(&mux);
  if (finished || now - lastReport >= 1000) {
    lastReport = now;
    LOG_INF("SCAN", "disc=%u result=%u targetDisc=%u targetResult=%u", disc, result, targetDisc, targetResult);
    if (targetDisc || targetResult) {
      LOG_INF("SCAN", "target=%s type=%u rssi=%d hid=%d conn=%d", target.address, target.addressType, target.rssi,
              target.hid, target.connectable);
    }
  }
  if (finished) {
    LOG_INF("SCAN", "end reason=%d free=%u minFree=%u maxBlock=%u", reason, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
            ESP.getMaxAllocHeap());
    LOG_INF("SCAN", "Raw scan complete; run CMD:BLE:OFF before returning to normal scan/connect");
  }
  return NimBLEDevice::getScan()->isScanning();
}
}  // namespace BleScanDiagnostics
#endif
