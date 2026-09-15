#pragma once

// Load the core defaults first: command-line -D options alone get overwritten
// by this platform's sdkconfig.h. These configure the source-built NimBLE host;
// they do not rebuild or shrink the precompiled radio controller.
#include <sdkconfig.h>

// NimBLE's compatibility header otherwise re-enables these from legacy names.
#undef CONFIG_NIMBLE_ROLE_PERIPHERAL
#undef CONFIG_NIMBLE_ROLE_BROADCASTER

#undef CONFIG_BT_NIMBLE_EXT_ADV
#define CONFIG_BT_NIMBLE_EXT_ADV 1
// Extended advertising requires the larger HCI event buffer.
#undef CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE 257
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
