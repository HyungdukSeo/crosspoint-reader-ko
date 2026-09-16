#include "BleProbe.h"

#include "BleScanDiagnostics.h"

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
bool uiOwnsInput = false;
bool uiInput = false;
void memory(const char* stage) {
  LOG_INF("BLE", "%s: free=%u min=%u maxBlock=%u running=%d connected=%d connecting=%d", stage, ESP.getFreeHeap(),
          ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), BleHid.isRunning(), BleHid.isConnected(), BleHid.isConnecting());
}
}  // namespace

void setUiOwnsInput(bool owns) {
  uiOwnsInput = owns;
  uiInput = false;
}
void reportUiInput() { uiInput = true; }

void stop() {
  if (BleHid.isRunning()) {
    powerManager.setPowerSaving(false);
    BleHid.end();
#ifdef CP_BLE_SCAN_DIAGNOSTICS
    BleScanDiagnostics::reset();
#endif
    memory("off");
  }
}

void command(const String& command) {
#ifdef CP_BLE_DIRECT_CONNECT
  if (command.startsWith("BLE:OPTIONS:")) {
    int type, first;
    char extra;
    const bool valid = sscanf(command.c_str(), "BLE:OPTIONS:%d:%d%c", &type, &first, &extra) == 2 && type >= -1 &&
                       type <= 1 && (first == 0 || first == 1);
    const bool ok = valid && BleHid.setDiagnosticOptions(type, first != 0);
    LOG_INF("BLE", "options accepted=%d type=%d securityFirst=%d; format BLE:OPTIONS:<-1|0|1>:<0|1>", ok,
            BleHid.diagnosticAddressType(), BleHid.diagnosticSecurityFirst());
    return;
  }
  if (command == "BLE:STATUS") {
    memory("status");
    LOG_INF("BLE", "options type=%d securityFirst=%d", BleHid.diagnosticAddressType(),
            BleHid.diagnosticSecurityFirst());
    return;
  }
#endif
  if (uiOwnsInput) {
    LOG_INF("BLE", "Close the Bluetooth keyboard screen before using serial commands");
    return;
  }
#ifdef CP_BLE_SCAN_DIAGNOSTICS
  if (BleScanDiagnostics::command(command)) return;
  if (BleScanDiagnostics::active() && command != "BLE:OFF" && command != "BLE:STATUS") {
    LOG_INF("SCAN", "Run CMD:BLE:OFF before leaving raw scan mode");
    return;
  }
#endif
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
    LOG_INF("BLE", "scan requested: scanning=%d", BleHid.isScanning());
    memory("after scan request");
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
  if (uiOwnsInput) {
    const bool activity = uiInput;
    uiInput = false;
    return activity;
  }
#ifdef CP_BLE_SCAN_DIAGNOSTICS
  if (BleScanDiagnostics::active()) {
    return BleScanDiagnostics::poll();
  }
#endif
#ifdef CP_BLE_SCAN_DIAGNOSTICS
  // Keep diagnostic ON idle: only explicit connection requests may start a link.
  if (BleHid.isConnecting() || BleHid.isConnected()) BleHid.poll();
#else
  BleHid.poll();
#endif
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
