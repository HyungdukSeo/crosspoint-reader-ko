#include "BluetoothKeyboardActivity.h"

#ifdef CP_BLE_PROBE
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BleProbe.h"

void BluetoothKeyboardActivity::onEnter() {
  Activity::onEnter();
  BleProbe::stop();
  BleProbe::setUiOwnsInput(true);
  state = State::Menu;
  requestUpdate();
}

void BluetoothKeyboardActivity::onExit() {
  // The callback object belongs to this activity. Stop BLE before it is freed.
  BleProbe::stop();
  BleProbe::setUiOwnsInput(false);
  Activity::onExit();
}

bool BluetoothKeyboardActivity::preventAutoSleep() { return state == State::Scanning || state == State::Connecting; }

bool BluetoothKeyboardActivity::startRadio() {
  BleProbe::stop();
  powerManager.setPowerSaving(false);
  if (!BleHid.begin("CrossPoint")) {
    status = tr(STR_BLE_START_FAILED);
    state = State::Error;
    requestUpdate();
    return false;
  }
  return true;
}

void BluetoothKeyboardActivity::ingest(const NimBLEAdvertisedDevice* device) {
  if (!device) return;
  ++signals;
  const auto address = device->getAddress();
  const auto addr = address.toString();
  const auto name = device->getName();
  const bool hid = device->isAdvertisingService(NimBLEUUID(uint16_t{0x1812})) ||
                   (device->haveAppearance() && device->getAppearance() == 0x03c1);
  // Capture initial advertisements too: onResult alone may wait for a scan
  // response. Both paths bypass any name/HID advertising requirement.
  portENTER_CRITICAL(&scanMux);
  BleHid.onScanResultIngest(addr.c_str(), name.c_str(), device->getRSSI(), address.getType(), hid,
                            device->isConnectable());
  portEXIT_CRITICAL(&scanMux);
}

void BluetoothKeyboardActivity::startScan(int mode) {
  if (!startRadio()) return;
  scanMode = mode;
  savedList = false;
  signals = 0;
  deviceCount = selected = 0;
  status.clear();
  auto* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(this, true);
  scan->setActiveScan(mode != 1);
  scan->setDuplicateFilter(0);
  scan->setFilterPolicy(0);
  scan->setLimitedOnly(false);
  scan->setInterval(160);
  scan->setWindow(160);
  scan->setPhy(mode == 2 ? NimBLEScan::SCAN_1M : NimBLEScan::SCAN_ALL);
  scan->setMaxResults(0);
  scanStarted = millis();
  if (!scan->start(15000, false, true)) {
    status = tr(STR_BLE_SCAN_FAILED);
    state = State::Error;
  } else {
    state = State::Scanning;
  }
  requestUpdate();
}

void BluetoothKeyboardActivity::collectResults() {
  BleHid.stopScan();
  portENTER_CRITICAL(&scanMux);
  deviceCount = BleHid.deviceCount();
  for (int i = 0; i < deviceCount; ++i) devices[i] = BleHid.device(i);
  portEXIT_CRITICAL(&scanMux);
  std::sort(devices.begin(), devices.begin() + deviceCount, [](const auto& a, const auto& b) {
    if (a.connectable != b.connectable) return a.connectable;
    return a.rssi > b.rssi;
  });
  state = State::Devices;
  selected = 0;
  requestUpdate();
}

void BluetoothKeyboardActivity::showSaved() {
  if (!startRadio()) return;
  savedList = true;
  deviceCount = BleHid.pairedCount();
  for (int i = 0; i < deviceCount; ++i) {
    const auto& bond = BleHid.paired(i);
    devices[i] = {};
    snprintf(devices[i].addr, sizeof(devices[i].addr), "%s", bond.addr);
    snprintf(devices[i].name, sizeof(devices[i].name), "%s", bond.name);
    devices[i].connectable = true;
    devices[i].addrType = bond.addrType;
  }
  selected = 0;
  state = State::Devices;
  requestUpdate();
}

int BluetoothKeyboardActivity::itemCount() const {
  return state == State::Menu ? 5 : state == State::Devices ? deviceCount + 1 : 0;
}

void BluetoothKeyboardActivity::activate() {
  if (state == State::Menu) {
    if (selected < 3)
      startScan(selected);
    else if (selected == 3)
      showSaved();
    else
      finish();
  } else if (state == State::Devices) {
    if (selected == deviceCount) {
      startScan(scanMode);
    } else if (!devices[selected].connectable) {
      status = tr(STR_BLE_NOT_CONNECTABLE);
      state = State::Error;
    } else {
      peer = devices[selected].name[0] ? devices[selected].name : devices[selected].addr;
      passkey.clear();
      text.clear();
      lastKey.clear();
      if (BleHid.connect(devices[selected].addr)) {
        state = State::Connecting;
        connectStarted = millis();
      } else {
        status = tr(STR_CONNECTION_FAILED);
        state = State::Error;
      }
    }
    requestUpdate();
  }
}

void BluetoothKeyboardActivity::loop() {
  RenderLock lock(*this);
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (state == State::Menu) {
      lock.unlock();
      finish();
      return;
    }
    BleProbe::stop();
    state = State::Menu;
    selected = 0;
    requestUpdate();
    return;
  }
  if (state == State::Scanning) {
    if (!NimBLEDevice::getScan()->isScanning() || mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      collectResults();
    } else if (millis() - lastPaint >= 1000) {
      lastPaint = millis();
      requestUpdate();
    }
    return;
  }
  if (state == State::Connecting || state == State::Connected) {
    // Main-loop serial diagnostics yield ownership here, including passkeys and
    // keys. Don't call SDK poll while listing devices: it auto-reconnects bonds.
    BleHid.poll();
    uint32_t code;
    if (BleHid.takePairingPasskey(code)) {
      char value[7];
      snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(code));
      passkey = value;
      requestUpdate();
    }
    char failure[48];
    if (BleHid.takeConnectFailure(failure, sizeof(failure))) {
      status = std::string(tr(STR_CONNECTION_FAILED)) + ": " + failure;
      state = State::Error;
      requestUpdate();
    } else if (state == State::Connecting && BleHid.isConnected()) {
      BleHid.releaseScanResults();
      state = State::Connected;
      requestUpdate();
    } else if (state == State::Connecting && millis() - connectStarted > 45000) {
      BleProbe::stop();
      status = tr(STR_BLE_CONNECT_TIMEOUT);
      state = State::Error;
      requestUpdate();
    } else if (state == State::Connected && !BleHid.isConnected()) {
      status = tr(STR_BLE_DISCONNECTED);
      state = State::Error;
      requestUpdate();
    }
    freeink::KeyEvent key;
    while (BleHid.popKey(key)) {
      BleProbe::reportUiInput();
      if (key.ch >= 32 && key.ch <= 126 && !(key.mods & 0xdd)) {
        if (text.size() >= 96) text.erase(0, 1);
        text += key.ch;
        lastKey = std::string(1, key.ch);
      } else {
        switch (key.special) {
          case freeink::SpecialKey::Backspace:
            if (!text.empty()) text.pop_back();
            lastKey = "Backspace";
            break;
          case freeink::SpecialKey::Enter:
            lastKey = "Enter";
            break;
          case freeink::SpecialKey::Left:
            lastKey = "Left";
            break;
          case freeink::SpecialKey::Right:
            lastKey = "Right";
            break;
          case freeink::SpecialKey::Up:
            lastKey = "Up";
            break;
          case freeink::SpecialKey::Down:
            lastKey = "Down";
            break;
          default:
            lastKey = tr(STR_BLE_KEY_RECEIVED);
            break;
        }
      }
      textDirty = true;
    }
    if (state == State::Connected && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      text.clear();
      lastKey.clear();
      textDirty = true;
    }
    if (textDirty && millis() - lastPaint >= 200) {
      textDirty = false;
      lastPaint = millis();
      requestUpdate();
    }
    return;
  }
  const int count = itemCount();
  if (count > 0) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // popActivity() can wait for rendering; don't hold its mutex on exit.
      if (state == State::Menu && selected == 4) lock.unlock();
      activate();
      return;
    }
    navigator.onNext([&] {
      selected = ButtonNavigator::nextIndex(selected, count);
      requestUpdate();
    });
    navigator.onPrevious([&] {
      selected = ButtonNavigator::previousIndex(selected, count);
      requestUpdate();
    });
  } else if (state == State::Error && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    state = State::Menu;
    selected = 0;
    requestUpdate();
  }
}

void BluetoothKeyboardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int line = renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight - line * 2;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_BLUETOOTH_KEYBOARD));
  auto draw = [&](int row, const std::string& value) {
    const auto clipped = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), width - metrics.contentSidePadding * 2);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, top + row * line, clipped.c_str());
  };
  const char* confirm = tr(STR_SELECT);
  if (state == State::Menu || state == State::Devices) {
    static constexpr StrId labels[] = {StrId::STR_BLE_SCAN_DEFAULT, StrId::STR_BLE_SCAN_PASSIVE, StrId::STR_BLE_SCAN_1M,
                                       StrId::STR_BLE_SAVED, StrId::STR_BLE_EXIT};
    GUI.drawList(
        renderer, Rect{0, top, width, height}, itemCount(), selected,
        [&](int index) -> std::string {
          if (state == State::Menu) return I18N.get(labels[index]);
          if (index == deviceCount) return tr(STR_BLE_SCAN_AGAIN);
          return devices[index].name[0] ? devices[index].name : devices[index].addr;
        },
        [&](int index) -> std::string {
          if (state == State::Menu) return "";
          if (index == deviceCount) return deviceCount ? "" : tr(STR_BLE_NO_DEVICES);
          if (savedList) return devices[index].addr;
          char info[64];
          snprintf(info, sizeof(info), "%s  %d dBm%s", devices[index].addr, devices[index].rssi,
                   devices[index].connectable ? "" : " [non-connectable]");
          return info;
        });
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, top + height,
                      I18N.get(state == State::Menu ? StrId::STR_BLE_EXIT_HINT : StrId::STR_BLE_UNNAMED_HINT));
  } else if (state == State::Scanning) {
    draw(0, tr(STR_SCANNING));
    draw(2, tr(STR_BLE_PAIRING_HINT));
    draw(3, tr(STR_BLE_UNNAMED_HINT));
    char progress[80];
    snprintf(progress, sizeof(progress), tr(STR_BLE_SCAN_PROGRESS), signals.load(),
             static_cast<unsigned>((millis() - scanStarted) / 1000));
    draw(5, progress);
    confirm = tr(STR_BLE_SHOW_RESULTS);
  } else if (state == State::Connecting) {
    draw(0, tr(STR_CONNECTING));
    draw(1, peer);
    if (!passkey.empty()) {
      draw(3, passkey);
      draw(4, tr(STR_BLE_PASSKEY_HINT));
    }
    confirm = "";
  } else if (state == State::Connected) {
    draw(0, tr(STR_CONNECTED));
    draw(1, peer);
    draw(2, tr(STR_BLE_TYPE_HINT));
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, text.c_str(), width - metrics.contentSidePadding * 2, 3);
    for (size_t i = 0; i < lines.size(); ++i) draw(3 + i, lines[i]);
    draw(7, std::string(tr(STR_BLE_LAST_KEY)) + lastKey);
    confirm = tr(STR_CLEAR_BUTTON);
  } else {
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, status.c_str(), width - metrics.contentSidePadding * 2, 4);
    for (size_t i = 0; i < lines.size(); ++i) draw(i, lines[i]);
    confirm = tr(STR_RETRY);
  }
  const auto labels =
      mappedInput.mapLabels(I18N.get(state == State::Connected ? StrId::STR_BLE_DISCONNECT : StrId::STR_BACK), confirm,
                            itemCount() ? tr(STR_DIR_UP) : "", itemCount() ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
#endif
