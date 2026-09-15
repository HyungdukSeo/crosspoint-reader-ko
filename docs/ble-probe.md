# BLE keyboard footprint probe

`ble_probe` builds the release firmware with the SDK BLE HID host and a serial
diagnostic interface. It measures the cost of discovery, pairing, reconnection,
and receiving key events before adding settings UI or an editor. It does not
insert received keys into the app's text fields.

## Build

```sh
pio run -e gh_release
pio run -e ble_probe
```

Compare the actual `.pio/build/<environment>/firmware.bin` sizes against
**6,553,600 bytes**, the stock OTA limit. PlatformIO's percentage and the image
validator use this repository's larger partition table, so a successful build
alone does not establish stock OTA compatibility.

The probe uses NimBLE-Arduino 2.5.1, central/observer roles, one connection,
four bond slots, and extended advertising. `config/ble_probe_config.h` loads
the core defaults before overriding the source-built host settings; plain `-D`
flags are overwritten by this platform's `sdkconfig.h`. This does not reduce
the precompiled controller's own allocation limits. The `BLE` library ignore
is cleared because this platform also removes the IDF `bt` component when it
is ignored. Instead, the build script excludes only the source files of
Arduino's bundled BLE adapter, preserving the controller and core Bluetooth
initialization. Otherwise its objects pull a second FreeRTOS NPL
implementation from `libbt.a` and fail to link alongside NimBLE-Arduino.
`scripts/ble_probe_config.py` hashes the forced-include header into the compiler
flags so changes invalidate cached objects (SCons does not track that header
automatically).

The SDK's passkey-display callback requires NimBLE 2.4.0 or newer despite its
older documented minimum. The pinned 2.5.1 also fixes a scan timer crash during
reinitialization; see the [NimBLE release notes](https://github.com/h2zero/NimBLE-Arduino/releases).

## Hardware procedure

Requires a BLE HID keyboard and a device with working USB serial. A USB-locked
X3 cannot use this interface; an on-device pairing screen would be needed.
Building does not install the image onto a device.

After installing the probe firmware and booting with USB attached, open a
115200-baud serial terminal with newline-terminated input. Start from the home
screen with Wi-Fi off, and send each command separately:

```text
CMD:BLE:STATUS
CMD:BLE:ON
CMD:BLE:SCAN
```

Put the keyboard in pairing mode. Wait about five seconds, then send
`CMD:BLE:LIST` to stop scanning and print addresses. Use an address from that
list:

```text
CMD:BLE:CONNECT:AA:BB:CC:DD:EE:FF
```

`connect request=1` means queued, not connected. Wait for the `connected` log.
If a six-digit pairing code appears, type it on the keyboard and press Enter.
Type a short disposable sample using letters, Shift, arrows, Enter, and
Backspace. The probe prints HID usage, modifiers, ASCII, and special-key enum
values. All received keys are logged by this diagnostic firmware.

Capture `CMD:BLE:STATUS` at home, during/after connecting, and with a book open.
Use `CMD:BLE:OFF` to tear down the stack; compare heap before and after. Repeat
ON/OFF and keyboard power-off/reconnection cycles to check recovery and leaks.
Bond records persist across OFF and reboot, and ON permits SDK auto-reconnect.

Each memory log contains free heap, the lowest free heap since boot, and the
largest allocatable block. The minimum is cumulative and does not rise after
OFF; compare current free heap and largest block for recovery. Repeat from a
fresh boot when comparing different workloads. Linker RAM totals do not measure
runtime BLE allocations.

While BLE runs, CPU downclocking to 10 MHz is disabled. Received keys reset the
inactivity timer. Normal automatic/manual sleep still stops BLE and enters deep
sleep; wake with the device power button and send ON again. This probe does not
implement keyboard wake from deep sleep or assess battery life.

## Measurements

Measured on 2026-09-15, application base `84a3919`, SDK `a485dc4`, with the
probe changes in the working tree. Both use release logging and the same fonts.

| Build | firmware.bin | Headroom below stock OTA limit |
|---|---:|---:|
| `gh_release` | 5,887,440 bytes | 666,160 bytes |
| `ble_probe` | 6,118,448 bytes | 435,152 bytes |
| Increase | **231,008 bytes (225.6 KiB)** | |

The probe leaves **425.0 KiB** for further changes. This measures the linked BLE
host and diagnostic interface, not a pairing UI, Hangul composition, or a text
editor. Both images pass the existing X3 checksum/header/hash validation.

The linker's combined SRAM-resident code/data total rises from **116,773** to
**147,481 bytes**, an increase of **30,708 bytes**. This includes 20,340 bytes
of additional RAM-resident code and 10,368 bytes of data/BSS. PlatformIO's simple
RAM percentage only reports the latter category; it is not the whole SRAM
cost. Dynamic BLE allocations are additional and remain unmeasured.

No image was flashed and no keyboard connection was tested as part of this
measurement. Runtime connection, heap, sleep/reconnect, and battery measurements
require the hardware procedure above. Flash headroom supports continuing with
a small editor using the existing fonts, but does not establish runtime memory
safety alongside EPUB rendering or Wi-Fi/TLS.
