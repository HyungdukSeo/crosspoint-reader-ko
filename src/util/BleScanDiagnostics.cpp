#include "BleScanDiagnostics.h"

#ifdef CP_BLE_SCAN_DIAGNOSTICS
#include <BleKeyboardHost.h>
#include <Logging.h>
#include <NimBLEDevice.h>

#include <array>
#include <cstring>

namespace BleScanDiagnostics {
namespace {
// Callbacks only snapshot data; serial output runs in the app loop so USB
// logging cannot block the NimBLE host task. Burst losses are counted explicitly.
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
std::array<Record, 16> records;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
unsigned head = 0, tail = 0, count = 0;
unsigned discoveries = 0, results = 0, dropped = 0;
bool mode = false;
bool ended = false;
int endReason = 0;

class Callbacks : public NimBLEScanCallbacks {
  static void capture(const NimBLEAdvertisedDevice* device, bool discovered) {
    if (!device) return;
    Record record;
    const auto address = device->getAddress();
    const auto name = device->getName();
    snprintf(record.address, sizeof(record.address), "%s", address.toString().c_str());
    snprintf(record.name, sizeof(record.name), "%s", name.c_str());
    record.addressType = address.getType();
    record.rssi = device->getRSSI();
    record.hid = device->isAdvertisingService(NimBLEUUID(uint16_t{0x1812}));
    record.connectable = device->isConnectable();
    record.legacy = device->isLegacyAdvertisement();
    record.primary = device->getPrimaryPhy();
    record.secondary = device->getSecondaryPhy();
    record.dataStatus = device->getDataStatus();
    record.discovered = discovered;
    const auto& payload = device->getPayload();
    record.bytes = payload.size();
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < payload.size() && i < 16; ++i) {
      record.payload[i * 2] = hex[payload[i] >> 4];
      record.payload[i * 2 + 1] = hex[payload[i] & 15];
    }
    portENTER_CRITICAL(&mux);
    if (discovered)
      ++discoveries;
    else
      ++results;
    if (count < records.size()) {
      records[head] = record;
      head = (head + 1) % records.size();
      ++count;
    } else {
      ++dropped;
    }
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
  head = tail = count = discoveries = results = dropped = 0;
  ended = false;
  portEXIT_CRITICAL(&mux);
}

bool command(const String& command) {
  if (!command.startsWith("BLE:RAW")) return false;
  const bool passive = command == "BLE:RAW:PASSIVE";
  const bool oneM = command == "BLE:RAW:1M";
  if (!passive && !oneM && command != "BLE:RAW:ACTIVE") {
    LOG_INF("SCAN", "Use CMD:BLE:RAW:ACTIVE, PASSIVE or 1M");
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
  scan->setInterval(160);
  scan->setWindow(160);
  scan->setPhy(oneM ? NimBLEScan::SCAN_1M : NimBLEScan::SCAN_ALL);
  // Callback-only mode bounds NimBLE's retained scan results; our log ring is
  // fixed-size. Pending active-scan responses may still need temporary storage.
  scan->setMaxResults(0);
  const bool started = scan->start(15000, false, true);
  LOG_INF("SCAN", "mode=%s start=%d scanning=%d duration=15000ms free=%u maxBlock=%u", command.c_str(), started,
          scan->isScanning(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

bool poll() {
  // Bound the work per loop so a crowded radio environment cannot starve UI.
  for (size_t i = 0; i < records.size(); ++i) {
    Record record;
    portENTER_CRITICAL(&mux);
    const bool available = count > 0;
    if (available) {
      record = records[tail];
      tail = (tail + 1) % records.size();
      --count;
    }
    portEXIT_CRITICAL(&mux);
    if (!available) break;
    LOG_INF("SCAN", "%s addr=%s type=%u name='%s' rssi=%d hid=%d conn=%d legacy=%d phy=%u/%u data=%u bytes=%u hex=%s",
            record.discovered ? "DISC" : "RESULT", record.address, record.addressType, record.name, record.rssi,
            record.hid, record.connectable, record.legacy, record.primary, record.secondary, record.dataStatus,
            record.bytes, record.payload);
  }
  portENTER_CRITICAL(&mux);
  const bool finished = ended && count == 0;
  const auto disc = discoveries, result = results, lost = dropped;
  const int reason = endReason;
  if (finished) ended = false;
  portEXIT_CRITICAL(&mux);
  if (finished) {
    LOG_INF("SCAN", "end reason=%d discovered=%u results=%u logDropped=%u free=%u maxBlock=%u", reason, disc, result,
            lost, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    LOG_INF("SCAN", "Raw scan complete; run CMD:BLE:OFF before returning to normal scan/connect");
  }
  return NimBLEDevice::getScan()->isScanning();
}
}  // namespace BleScanDiagnostics
#endif
