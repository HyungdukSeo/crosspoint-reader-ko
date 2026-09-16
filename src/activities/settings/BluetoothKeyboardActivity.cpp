#include "BluetoothKeyboardActivity.h"

#ifdef CP_BLE_PROBE
#include <FontCacheManager.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <NimBLEDevice.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BleProbe.h"

void BluetoothKeyboardActivity::onEnter() {
  Activity::onEnter();
  stopUiScan();
  BleProbe::setUiOwnsInput(true);
  state = State::Menu;
  savedConnectIndex = -1;
  // Opening settings only observes the service. It must neither initialize an
  // OFF radio nor restart an existing connection.
  if (BleHid.isConnecting()) {
    state = State::Connecting;
    peer = BleHid.connectedName();
    connectStarted = millis();
  }
  requestUpdate();
}

void BluetoothKeyboardActivity::onExit() {
  // Bluetooth is a user-controlled service. Leaving this screen must not turn
  // it off or discard the active HID connection; only stop an in-progress scan.
  stopUiScan();
  BleProbe::setUiOwnsInput(false);
  Activity::onExit();
}

void BluetoothKeyboardActivity::stopUiScan() {
  if (!BleHid.isRunning()) return;
  BleHid.stopScan();
  // All scan events go to the SDK-owned callback; none retain this activity.
  BleHid.restoreScanCallbacks();
}

bool BluetoothKeyboardActivity::preventAutoSleep() { return state == State::Scanning || state == State::Connecting; }

bool BluetoothKeyboardActivity::startRadio() {
  if (BleHid.isRunning()) return true;
  powerManager.setPowerSaving(false);
#ifdef CP_BLE_DIRECT_CONNECT
  const auto beforeCache = ESP.getFreeHeap();
  if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
  LOG_INF("BLE", "before init: cache release free=%u -> %u maxBlock=%u", beforeCache, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
#endif
  if (!BleHid.begin("CrossPoint")) {
    status = tr(STR_BLE_START_FAILED);
    state = State::Error;
    requestUpdate();
    return false;
  }
  return true;
}

bool BluetoothKeyboardActivity::startConnection(const std::string& address, const std::string& name) {
  retryAddr = address;
  retryPeer = name;
  peer = name.empty() ? address : name;
  passkey.clear();
  text.clear();
  lastKey.clear();
  notification.clear();
  if (!startRadio()) return false;
  stopUiScan();
#ifdef CP_BLE_DIRECT_CONNECT
  // Keep every UI entry point on the same direct9 settings, including retries
  // and saved-device auto-connect. Connection negotiation belongs to the SDK.
  BleHid.setDiagnosticOptions(-1, true);
#endif
  if (!BleHid.connect(retryAddr.c_str())) {
    status = tr(STR_CONNECTION_FAILED);
    state = State::Error;
    requestUpdate();
    return false;
  }
  state = State::Connecting;
  connectStarted = millis();
  requestUpdate();
  return true;
}

void BluetoothKeyboardActivity::startScan(int mode) {
  retryAddr.clear();
  retryPeer.clear();
  savedConnectIndex = -1;
  notification.clear();
  // This host has one client. Explicitly adding another device first releases
  // the existing link; saved registrations survive the radio restart.
  if (BleHid.isConnected()) {
    stopUiScan();
    BleProbe::stop();
  }
  if (!startRadio()) return;
  stopUiScan();
  BleHid.releaseScanResults();
  scanMode = mode;
  deviceCount = selected = 0;
  status.clear();
  auto* scan = NimBLEDevice::getScan();
  scan->setActiveScan(mode != 1);
  scan->setDuplicateFilter(0);
  scan->setFilterPolicy(0);
  scan->setLimitedOnly(false);
#ifdef CP_BLE_SCAN_DIAGNOSTICS
  scan->setInterval(30);
  scan->setWindow(30);
#else
  scan->setInterval(160);
  scan->setWindow(160);
#endif
#if CONFIG_BT_NIMBLE_EXT_ADV
  scan->setPhy(mode == 2 ? NimBLEScan::SCAN_1M : NimBLEScan::SCAN_ALL);
#endif
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
  stopUiScan();
  deviceCount = BleHid.copyDiscoveredDevices(devices.data(), static_cast<uint8_t>(devices.size()));
  std::sort(devices.begin(), devices.begin() + deviceCount, [](const auto& a, const auto& b) {
    if (a.connectable != b.connectable) return a.connectable;
    return a.rssi > b.rssi;
  });
#ifdef CP_BLE_SCAN_DIAGNOSTICS
  LOG_INF("SCAN", "mode=%d retained=%d", scanMode, deviceCount);
  for (int i = 0; i < deviceCount; ++i) {
    LOG_INF("SCAN", "device=%s name=%s hid=%d rssi=%d", devices[i].addr, devices[i].name, devices[i].hid,
            devices[i].rssi);
  }
#endif
  state = State::Devices;
  selected = 0;
  requestUpdate();
}

void BluetoothKeyboardActivity::showSaved() {
  // Loading the saved list does not require a running Bluetooth controller.
  BleHid.loadSavedDevices();
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
  state = State::SavedDevices;
  requestUpdate();
}

void BluetoothKeyboardActivity::connectSaved(int index) {
  if (index < 0 || index >= BleHid.pairedCount()) {
    savedConnectIndex = -1;
    state = State::Menu;
    requestUpdate();
    return;
  }
  for (; index < BleHid.pairedCount(); ++index) {
    savedConnectIndex = index;
    const auto& bond = BleHid.paired(index);
    if (startConnection(bond.addr, bond.name[0] ? bond.name : bond.addr)) return;
    if (!BleHid.isRunning()) break;
  }
  savedConnectIndex = -1;
  notification = status;
  notificationUntil = millis() + 3500;
  requestUpdate();
}

int BluetoothKeyboardActivity::itemCount() const {
  if (state == State::Menu) return 3;
  if (state == State::Devices) return deviceCount + 1;
  if (state == State::SavedDevices) return deviceCount + 1;
  return 0;
}

void BluetoothKeyboardActivity::activate() {
  if (state == State::Menu) {
    if (selected == 0) {
      notification.clear();
      retryAddr.clear();
      retryPeer.clear();
      if (BleHid.isRunning()) {
        stopUiScan();
        BleProbe::stop();
        savedConnectIndex = -1;
      } else {
        if (startRadio() && BleHid.pairedCount() > 0) connectSaved(0);
      }
      requestUpdate();
    } else if (selected == 1) {
      startScan(0);
    } else {
      showSaved();
    }
  } else if (state == State::Devices) {
    if (selected == deviceCount) {
      startScan(scanMode);
    } else if (!devices[selected].connectable) {
      retryAddr.clear();
      retryPeer.clear();
      status = tr(STR_BLE_NOT_CONNECTABLE);
      state = State::Error;
    } else {
      savedConnectIndex = -1;
      startConnection(devices[selected].addr, devices[selected].name);
    }
    requestUpdate();
  } else if (state == State::SavedDevices) {
    if (selected == deviceCount) {
      state = State::Menu;
      selected = 2;
    } else {
      const int deletedIndex = selected;
      const bool deleted = BleHid.forget(devices[selected].addr);
      showSaved();
      selected = std::min(deletedIndex, deviceCount);
      if (!deleted) {
        notification = std::string(tr(STR_DELETE)) + ": " + tr(STR_FAILED_LOWER);
        notificationUntil = millis() + 3500;
      }
    }
    requestUpdate();
  }
}

void BluetoothKeyboardActivity::loop() {
  RenderLock lock(*this);
  if (!notification.empty() && static_cast<int32_t>(millis() - notificationUntil) >= 0) {
    notification.clear();
    requestUpdate();
  }
  // The async worker reports its result to this screen. Keep it visible until
  // that result arrives instead of abandoning a pending pairing in the menu.
  if (state != State::Connecting && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (state == State::Menu) {
      lock.unlock();
      finish();
      return;
    }
    if (state == State::SavedDevices) {
      state = State::Menu;
      selected = 2;
      requestUpdate();
      return;
    }
    if (state == State::Scanning) stopUiScan();
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
      deviceCount = BleHid.copyDiscoveredDevices(devices.data(), static_cast<uint8_t>(devices.size()));
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
    if (!BleHid.isConnecting() && BleHid.takeConnectFailure(failure, sizeof(failure))) {
      if (savedConnectIndex >= 0 && savedConnectIndex + 1 < BleHid.pairedCount()) {
        connectSaved(savedConnectIndex + 1);
      } else {
        savedConnectIndex = -1;
        status = std::string(tr(STR_CONNECTION_FAILED)) + ": " + failure;
        notification = status;
        notificationUntil = millis() + 3500;
        state = State::Error;
      }
      requestUpdate();
    } else if (state == State::Connecting && !BleHid.isConnecting() && BleHid.isConnected()) {
      BleHid.releaseScanResults();
      savedConnectIndex = -1;
      state = State::Connected;
      notification = peer + "  " + tr(STR_CONNECTED);
      notificationUntil = millis() + 3500;
      requestUpdate();
    } else if (state == State::Connecting && millis() - connectStarted >
#ifdef CP_BLE_DIRECT_CONNECT
                                                 90000
#else
                                                 45000
#endif
    ) {
      stopUiScan();
      BleProbe::stop();
      if (savedConnectIndex >= 0 && savedConnectIndex + 1 < BleHid.pairedCount()) {
        connectSaved(savedConnectIndex + 1);
      } else {
        savedConnectIndex = -1;
        status = tr(STR_BLE_CONNECT_TIMEOUT);
        notification = status;
        notificationUntil = millis() + 3500;
        state = State::Error;
      }
      requestUpdate();
    } else if (state == State::Connected && !BleHid.isConnected()) {
      status = tr(STR_BLE_DISCONNECTED);
      notification = status;
      notificationUntil = millis() + 3500;
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
    // Retry the last connection directly. Searching again is unnecessary and
    // can miss a rotating/private address while the HID is still advertising.
    if (!retryAddr.empty()) {
      savedConnectIndex = -1;
      startConnection(retryAddr, retryPeer);
    } else {
      state = State::Menu;
      selected = 0;
    }
    requestUpdate();
  }
}

void BluetoothKeyboardActivity::render(RenderLock&&) {
#ifdef CP_BLE_DIRECT_CONNECT
  // Rendering owns the render lock. Glyph buffers are no longer needed once
  // pixels have been copied, and keeping them can starve BLE initialization.
  struct ReleaseFontCache {
    FontCacheManager* cache;
    ~ReleaseFontCache() {
      if (cache) cache->clearCache();
    }
  } releaseFontCache{renderer.getFontCacheManager()};
#endif
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
  if (state == State::Menu || state == State::Devices || state == State::SavedDevices) {
    GUI.drawList(
        renderer, Rect{0, top, width, height}, itemCount(), selected,
        [&](int index) -> std::string {
          if (state == State::Menu) {
            if (index == 0) return BleHid.isRunning() ? "Bluetooth: ON" : "Bluetooth: OFF";
            if (index == 1) return tr(STR_BLE_SCAN_DEFAULT);
            return tr(STR_BLE_SAVED);
          }
          if (state == State::SavedDevices) {
            if (index == deviceCount) return tr(STR_BLE_EXIT);
            const std::string label = devices[index].name[0] ? devices[index].name : devices[index].addr;
            return std::string(tr(STR_DELETE)) + ": " + label;
          }
          if (index == deviceCount) return tr(STR_BLE_SCAN_AGAIN);
          return devices[index].name[0] ? devices[index].name : devices[index].addr;
        },
        [&](int index) -> std::string {
          if (state == State::Menu) return "";
          if (state == State::SavedDevices) {
            if (index == deviceCount) return deviceCount ? "" : tr(STR_NO_ENTRIES);
            return devices[index].addr;
          }
          if (index == deviceCount) return deviceCount ? "" : tr(STR_BLE_NO_DEVICES);
          char info[64];
          snprintf(info, sizeof(info), "%s  %d dBm%s", devices[index].addr, devices[index].rssi,
                   devices[index].connectable ? "" : " [non-connectable]");
          return info;
        });
    const char* hint = state == State::Devices ? tr(STR_BLE_UNNAMED_HINT) : tr(STR_BLE_EXIT_HINT);
    const auto clippedHint = renderer.truncatedText(UI_10_FONT_ID, hint, width - metrics.contentSidePadding * 2);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, top + height, clippedHint.c_str());
    if (state == State::SavedDevices && selected < deviceCount) confirm = tr(STR_DELETE);
  } else if (state == State::Scanning) {
    draw(0, tr(STR_SCANNING));
    draw(2, tr(STR_BLE_PAIRING_HINT));
    draw(3, tr(STR_BLE_UNNAMED_HINT));
    char progress[80];
    snprintf(progress, sizeof(progress), tr(STR_BLE_SCAN_PROGRESS), static_cast<unsigned>(deviceCount),
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
    confirm = retryAddr.empty() ? tr(STR_BLE_EXIT) : tr(STR_RETRY);
  }
  if (!notification.empty() && static_cast<int32_t>(notificationUntil - millis()) > 0) {
    const auto popupStyle = metrics.popupTextBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto clipped = renderer.truncatedText(
        UI_12_FONT_ID, notification.c_str(),
        std::max(0, width - 2 * (metrics.popupMarginX + metrics.popupFrameThickness)), popupStyle);
    GUI.drawPopup(renderer, clipped.c_str());
  }
  const auto labels = mappedInput.mapLabels(state == State::Connecting ? "" : tr(STR_BACK), confirm,
                                            itemCount() ? tr(STR_DIR_UP) : "", itemCount() ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
#endif
