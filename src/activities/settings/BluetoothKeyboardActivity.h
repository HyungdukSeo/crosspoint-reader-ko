#pragma once

#ifdef CP_BLE_PROBE
#include <BleKeyboardHost.h>

#include <array>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class BluetoothKeyboardActivity final : public Activity {
 public:
  BluetoothKeyboardActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("BluetoothKeyboard", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State { Menu, Scanning, Devices, SavedDevices, Connecting, Connected, Error };
  State state = State::Menu;
  ButtonNavigator navigator;
  std::array<freeink::DiscoveredDevice, freeink::BleKeyboardHost::kMaxDiscovered> devices{};
  int deviceCount = 0;
  int selected = 0;
  int scanMode = 0;
  int savedConnectIndex = -1;
  unsigned long scanStarted = 0, connectStarted = 0, lastPaint = 0;
  bool textDirty = false;
  std::string text;
  std::string status;
  std::string peer;
  std::string lastKey;
  std::string passkey;
  std::string retryAddr;
  std::string retryPeer;
  std::string notification;
  uint32_t notificationUntil = 0;

  bool startRadio();
  bool startConnection(const std::string& address, const std::string& name);
  void stopUiScan();
  void startScan(int mode);
  void showSaved();
  void connectSaved(int index);
  void collectResults();
  void activate();
  int itemCount() const;
};
#endif
