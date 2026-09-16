#pragma once

// Load the core defaults first: command-line -D options alone get overwritten
// by this platform's sdkconfig.h. These configure the source-built NimBLE host;
// they do not rebuild or shrink the precompiled radio controller.
#include <sdkconfig.h>

// HID/name metadata can arrive after the first advertisement. Let the pairing
// screen show anonymous candidates and validate HID service at connection time.
#define FREEINK_BLE_HID_SHOW_UNNAMED_DEVICES 1

// NimBLE's compatibility header otherwise re-enables these from legacy names.
#undef CONFIG_NIMBLE_ROLE_PERIPHERAL
#undef CONFIG_NIMBLE_ROLE_BROADCASTER

#undef CONFIG_BT_NIMBLE_EXT_ADV
#ifdef CP_BLE_LEGACY_SCAN
#define CONFIG_BT_NIMBLE_EXT_ADV 0
#else
#define CONFIG_BT_NIMBLE_EXT_ADV 1
#endif
// Extended advertising requires the larger HCI event buffer.
#undef CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE
#ifdef CP_BLE_LEGACY_SCAN
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE 70
#else
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE 257
#endif
#undef CONFIG_BT_NIMBLE_ROLE_CENTRAL
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 1
#undef CONFIG_BT_NIMBLE_ROLE_OBSERVER
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 1
#undef CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
#define CONFIG_BT_NIMBLE_ROLE_PERIPHERAL 0
#undef CONFIG_BT_NIMBLE_ROLE_BROADCASTER
#define CONFIG_BT_NIMBLE_ROLE_BROADCASTER 0
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
#undef CONFIG_BT_NIMBLE_MAX_BONDS
#define CONFIG_BT_NIMBLE_MAX_BONDS 4

// A diagnostic build must identify itself without duplicate -D version flags.
#ifdef CP_BLE_SCAN_DIAGNOSTICS
#undef CROSSPOINT_VERSION
#if defined(CP_BLE_DIRECT_CONNECT)
#define CROSSPOINT_VERSION "15.06"
#elif defined(CP_BLE_LEGACY_SCAN)
#define CROSSPOINT_VERSION "15.05-ble-legacy3"
#else
#define CROSSPOINT_VERSION "15.05-ble-debug2"
#endif
#endif
