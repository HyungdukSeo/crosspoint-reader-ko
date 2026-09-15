#pragma once

#ifdef CP_BLE_PROBE
#include <BleKeyboardHost.h>
#include <NimBLEDevice.h>

#include <array>
#include <atomic>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class BluetoothKeyboardActivity final : public Activity, private NimBLEScanCallbacks {
 public:
  BluetoothKeyboardActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("BluetoothKeyboard", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State { Menu, Scanning, Devices, Connecting, Connected, Error };
  State state = State::Menu;
  ButtonNavigator navigator;
  std::array<freeink::DiscoveredDevice, freeink::BleKeyboardHost::kMaxDiscovered> devices{};
  portMUX_TYPE scanMux = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<unsigned> signals{0};
  int deviceCount = 0;
  int selected = 0;
  int scanMode = 0;
  bool savedList = false;
  unsigned long scanStarted = 0, connectStarted = 0, lastPaint = 0;
  bool textDirty = false;
  std::string text;
  std::string status;
  std::string peer;
  std::string lastKey;
  std::string passkey;

  bool startRadio();
  void startScan(int mode);
  void showSaved();
  void collectResults();
  void activate();
  int itemCount() const;
  void ingest(const NimBLEAdvertisedDevice* device);
  void onDiscovered(const NimBLEAdvertisedDevice* device) override { ingest(device); }
  void onResult(const NimBLEAdvertisedDevice* device) override { ingest(device); }
};
#endif
