#include "BleProbe.h"

#ifdef CP_BLE_PROBE
#include <BleKeyboardHost.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <nimconfig.h>

static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS == 1, "Probe requires a single-connection NimBLE host");
static_assert(CONFIG_BT_NIMBLE_ROLE_CENTRAL && CONFIG_BT_NIMBLE_ROLE_OBSERVER && !CONFIG_BT_NIMBLE_ROLE_PERIPHERAL &&
                  !CONFIG_BT_NIMBLE_ROLE_BROADCASTER,
              "Probe requires central/observer roles only");

namespace BleProbe {
namespace {
void memory(const char* stage) {
  LOG_INF("BLE", "%s: free=%u min=%u maxBlock=%u running=%d connected=%d", stage, ESP.getFreeHeap(),
          ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), BleHid.isRunning(), BleHid.isConnected());
}
}  // namespace

void stop() {
  if (BleHid.isRunning()) {
    powerManager.setPowerSaving(false);
    BleHid.end();
    memory("off");
  }
}

void command(const String& command) {
  if (command == "BLE:ON") {
    powerManager.setPowerSaving(false);
    memory("before begin");
    const bool ok = BleHid.begin("CrossPoint BLE probe");
    LOG_INF("BLE", "begin=%d", ok);
    memory("after begin");
  } else if (command == "BLE:OFF") {
    stop();
  } else if (command == "BLE:STATUS") {
    memory("status");
  } else if (!BleHid.isRunning()) {
    LOG_INF("BLE", "Run CMD:BLE:ON first");
  } else if (command == "BLE:SCAN") {
    BleHid.startScan(5000);
    memory("scan started");
  } else if (command == "BLE:LIST") {
    // Avoid reading SDK scan records while its callback is updating them.
    BleHid.stopScan();
    for (uint8_t i = 0; i < BleHid.deviceCount(); ++i) {
      const auto& d = BleHid.device(i);
      LOG_INF("BLE", "%s %s rssi=%d hid=%d connectable=%d", d.addr, d.name, d.rssi, d.hid, d.connectable);
    }
    memory("scan stopped");
  } else if (command.startsWith("BLE:CONNECT:")) {
    BleHid.stopScan();
    const bool accepted = BleHid.connect(command.substring(12).c_str());
    LOG_INF("BLE", "connect request=%d (wait for connected log)", accepted);
  } else {
    LOG_INF("BLE", "Commands: ON OFF STATUS SCAN LIST CONNECT:<address>");
  }
}

bool poll() {
  BleHid.poll();
  static bool connected = false;
  if (connected != BleHid.isConnected()) {
    connected = BleHid.isConnected();
    if (connected) BleHid.releaseScanResults();
    memory(connected ? "connected" : "disconnected");
  }
  uint32_t passkey;
  if (BleHid.takePairingPasskey(passkey)) {
    LOG_INF("BLE", "Type %06lu on keyboard and press Enter", static_cast<unsigned long>(passkey));
  }
  char failure[48];
  if (BleHid.takeConnectFailure(failure, sizeof(failure))) {
    LOG_INF("BLE", "connect failed: %s", failure);
    memory("connection failure");
  }
  bool input = false;
  freeink::KeyEvent ev;
  while (BleHid.popKey(ev)) {
    input = true;
    LOG_INF("BLE", "key usage=%u mods=%u ascii=%u special=%u", ev.keycode, ev.mods, static_cast<unsigned char>(ev.ch),
            static_cast<unsigned>(ev.special));
  }
  return input;
}
}  // namespace BleProbe
#endif
