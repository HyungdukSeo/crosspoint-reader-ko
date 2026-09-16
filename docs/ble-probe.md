# BLE keyboard footprint probe

`ble_probe` builds the release firmware with the SDK BLE HID host, an on-device
pairing/input-test screen, and an optional serial diagnostic interface. It does
not insert received keys into other app text fields or provide a note editor.

## On-device test (15.05 and later)

USB serial is **not required** for this procedure.

1. Open **Settings → System → Bluetooth keyboard** (설정 → 시스템 → 블루투스 키보드).
2. Put the keyboard in pairing mode and choose **Search for keyboards**.
   For Clicks Power Keyboard, hold Power and press a number key (1–9), following
   the [manufacturer's pairing instructions](https://learn.clicks.tech/knowledge-base/kb-power-keyboard-getting-started-pairing-additional-devices).
3. The screen shows received discovery/result events and elapsed time. Search
   lasts 15 seconds; Confirm shows results early and Back cancels.
4. Select the device with Previous/Next and press Confirm. Anonymous devices are
   listed by address. The firmware verifies the HID service after connecting.
5. If a six-digit code appears, type it on the keyboard and press Enter.
6. After connecting, type English letters. The screen shows sample text and the
   last key received; Backspace edits the sample. Confirm clears the sample.
7. Back disconnects. Leaving this screen turns Bluetooth off. Saved pairings can
   be selected from **Paired keyboards** on the next visit.

If the keyboard is absent, return to the Bluetooth menu and compare **Search
without scan requests** (passive scan) and **Search on 1M PHY only**. These change
how discovery runs; success with another mode is evidence for further diagnosis,
not proof of a particular radio defect. Initial advertisements and final scan
results both feed the device list, before a name or HID advertisement is required.
The signal counter counts callbacks, including repeats, not unique devices.

This screen is an English input test, not a Hangul IME or general editor.
Actual Clicks/X3 discovery and pairing still require hardware verification.

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

## Optional USB serial procedure

Requires a BLE HID keyboard and a device with working USB serial. A USB-locked
X3 can use the on-device screen above instead.
Building does not install the image onto a device.

Close the on-device Bluetooth screen before issuing serial commands: the screen
owns key/passkey consumption while it is open.

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

The table below records the earlier serial-only probe. The on-device screen in
15.05 adds code and translations; use the actual 15.05 release asset size for
its budget, not this historical table.

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


## Local Clicks connection diagnostic build

Build `pio run -e ble_scan_debug`. The displayed version is `15.05-ble-debug2`;
this is a local diagnostic image, not a published OTA release.

The SDK connection instrumentation is preserved in
`docs/patches/ble-connection-diagnostics.patch`. On a fresh checkout, apply it
with `git -C freeink-sdk apply ../docs/patches/ble-connection-diagnostics.patch`
before building (do not apply twice). The current workspace already has it applied.
It only enables additional output under `CP_BLE_SCAN_DIAGNOSTICS`.

- On-device scans print a summary and retained devices when finished. `target`
  counts callbacks matching `d8:c3:e4:63:4d:ab` or a `Power Keyboard` name before
  the SDK's 24-device retention limit. It counts repeated callbacks, not unique
  devices. A zero count only describes that scan at the X3's location.
- Connection logs distinguish connect begin, alternate address type, HID
  discovery, security, input subscription, and failure. No passkey is required
  unless the negotiated pairing method requests one.
- Serial diagnostic mode leaves the radio idle after ON, avoiding automatic
  reconnection to saved devices. Close the Bluetooth UI before serial commands.

For a direct test, send `CMD:BLE:ON`, then
`CMD:BLE:CONNECT:d8:c3:e4:63:4d:ab`. Keep the keyboard in pairing mode. If the
operation stalls, `CMD:BLE:OFF` cancels it and releases radio memory.
Raw scans are available as `CMD:BLE:RAW:ACTIVE`, `CMD:BLE:RAW:PASSIVE`, and
`CMD:BLE:RAW:1M`; send OFF then ON between raw scans and before connecting.
Run `python3 scripts/check_ota_size.py .pio/build/ble_scan_debug/firmware.bin`
before installing. Physical validation is still required.

### Debug2 scan comparison

On-device diagnostic scans now use 30 ms interval and window. Serial RAW modes
also default to 30/30 ms; append `:160` to any RAW command for the previous
160/160 ms baseline (for example `CMD:BLE:RAW:PASSIVE:160`). Cycle OFF then ON
between scans. Keep keyboard position and pairing state unchanged for comparison.

RAW callbacks count all discovery/result events and independently count the
Clicks MAC or `Power Keyboard` name before any retention limit. Only the latest
target observation is stored. Once-per-second summaries replace the noisy
per-advertisement logs; there is no log ring whose overflow can hide a target
count. Counts include repeats and are not unique device counts. These counters
cannot detect packets discarded by the radio/controller before callbacks.
Memory is reported before scanning and on completion. `minFree` is the lifetime
minimum since boot, not the minimum for only that scan. Physical comparison
results are not yet available.


### X3 debug2 hardware comparison (2026-09-15)

Four sequential 15-second scans, cycling BLE OFF/ON between runs:

| Mode | Interval/window (ms) | Discovery callbacks | Result callbacks | Target callbacks |
| --- | --- | --- | --- | --- |
| Passive, all PHY | 30/30 | 1870 | 1870 | 0 |
| Passive, all PHY | 160/160 | 1835 | 1835 | 0 |
| Active, 1M | 30/30 | 3328 | 2848 | 0 |
| Active, 1M | 160/160 | 3542 | 2739 | 0 |

Target matches the captured MAC or a Power Keyboard name; callback counts include
repeats, not distinct devices. No target callback was observed in any run. This
comparison did not restore discovery; it does not exclude controller-level drops
or changes to the advertiser address/state. Simultaneous phone capture during
these exact scans was not verified.

Free heap before BLE: 116664 bytes. After BLE initialization: about 43660 bytes.
Lowest lifetime free heap at the end of these runs: 36168 bytes. After final
BLE OFF and deferred cleanup: 116404 bytes; largest free block 61428 bytes.
No crash/allocation failure was logged during the comparison. This does not
validate concurrent document editing or connected-keyboard memory usage.


The user subsequently reported pairing had switched off during the first
comparison; do not interpret that run as a controlled pairing-mode test.
After the user re-enabled pairing, repeated scans produced:
passive 30: 1764/1764; active 1M 30: 3411/2936; passive 160: 1783/1783;
active 1M 160: 3720/3108 discovery/result callbacks. All target counters were
zero again. Continued pairing throughout each scan and simultaneous phone
reception were not independently verified. BLE was turned off after testing.


Correction: the user later confirmed the repeat four-mode run also was not in
pairing mode. Neither four-mode run establishes a discovery failure while pairing.
After another explicit retry request, one 15-second active 1M 30/30 scan produced
3567 discovery and 3024 result callbacks, with both target counters zero.
The user was asked to maintain pairing during this short run; simultaneous
phone reception and continuous pairing status were not independently observed.
BLE OFF was sent after the run. No controller incompatibility is established.


### Legacy3 comparison firmware

Build `pio run -e ble_legacy_debug`; displayed version `15.05-ble-legacy3`.
Arduino core remains 3.3.7 (upstream based on ESP-IDF 5.5.2), NimBLE-Arduino 2.5.1.
The same patched SDK and controller package are retained. This is not a claim
that a core/library compatibility defect has been identified.

`CP_BLE_LEGACY_SCAN` sets `CONFIG_BT_NIMBLE_EXT_ADV=0`. NimBLEScan::start then
calls `ble_gap_disc`, whereas debug2 (including its 1M option) calls
`ble_gap_ext_disc`. The flag also affects other extended BLE host paths,
including connection setup; this is a host configuration comparison, not a
controller binary update. NimBLE also selects a 70-byte HCI event buffer in Legacy mode, versus
257 bytes in extended mode. The forced configuration matches this upstream
selection, so memory differences include that buffer change.

Serial commands and 30/160 ms comparison remain available; in this build all
scans use legacy 1M, so RAW:ACTIVE and RAW:1M use the same radio configuration.
The startup scan log must report `extendedAdvertising=0`. Target counters and
connection-stage logs remain enabled. Install this app-only image using the
existing SD firmware update menu. Do not overwrite published release assets.
Hardware discovery/connection results for this build are pending.

Legacy3 build verified: 6,127,136 bytes, stock OTA headroom 426,464 bytes.
ELF contains ble_gap_disc and excludes ble_gap_ext_disc. Version and diagnostic
markers verified in firmware.bin. Artifact SHA256:
002bf8198c94659f8fe6a8929f2dec1f3e8146ae28c7bb35e6c1014c3cee97af.
Physical installation and test are pending.


Legacy3 installed and tested: runtime confirmed extendedAdvertising=0.
One 15-second active Legacy 1M scan at 30/30 ms produced 3712 discovery
callbacks and 3185 result callbacks; both target counters remained zero.
Before BLE free heap 118696; after begin 52836; scan-end free 52788,
lifetime minimum 48644, largest block 49140 bytes. Compared with debug2,
post-initialization free heap increased by about 9 KB. This short scan did not
restore target discovery and does not establish a hardware/driver defect.
The user was asked to maintain pairing; no simultaneous phone trace was captured.
BLE OFF was sent after the scan.


### Direct4: Clicks pairing without a discovery list

Build `pio run -e ble_direct_debug`, version `15.05-ble-direct4`.
Uses the Legacy3 BLE configuration, retains diagnostic serial commands, and
shows three menu actions: Clicks direct connect, saved keyboards, exit.
The direct action targets the user-provided MAC `d8:c3:e4:63:4d:ab` without
starting a discovery scan. It then uses the existing pairing/passkey/input
screen. The target is specific to this diagnostic user's keyboard.

SDK link timeout is 20 seconds per address type (previously 8). Unknown
address types still try Public then Random. A stored bond can supply the known
type. UI timeout is 90 seconds, including service/security setup; this is a
ceiling, not a guarantee of successful pairing. Connection initiation still
listens for the peripheral's connectable advertising at the controller level;
bypassing the discovery list cannot bypass radio reachability.

Before installing direct4, a Legacy3 serial direct attempt timed out for both
address types with error 13; no HID discovery or pairing stage was reached.
Direct4 has not yet been physically tested. The intended procedure is to enter
pairing mode on the keyboard, then immediately select Clicks direct connect on
X3. Serial logging can remain open while the UI owns the radio.


Direct4 hardware result: UI direct connection to d8:c3:e4:63:4d:ab tried
Public then Random for 20 seconds each, both ending with error 13 timeout.
No HID-discovery or security stage was reached. Free heap stayed at 51148
bytes, lifetime minimum 51064, largest block 45044 during this attempt.
After the failure, SDK auto-reconnect unexpectedly started a different saved
peer (9b:91:66:de:cf:9e). The user was instructed to exit the Bluetooth screen.
This automatic retry is a separate diagnostic-flow bug and does not explain
the preceding Clicks connection timeouts. It must be disabled for direct mode.


### Direct5: second target and explicit-only connections

Version `15.05-ble-direct5` adds a second direct menu entry for
Keychron Nape Pro at `ea:22:99:9e:a8:90`, alongside Clicks. The provided HTML
space entity is not part of the name. Both entries share the existing pairing
and input-test flow; target menu selection is reset before entering that flow.

SDK automatic bonded-peer reconnect is disabled under CP_BLE_DIRECT_CONNECT,
including while the UI polls after a failure. Explicit saved-keyboard selection
remains available; no bonds are deleted. Per-address timeout remains 20 seconds.
Physical Keychron and Clicks connection results for this build are pending.


### Direct6: runtime connection options and service diagnostics

Version `15.05-ble-direct6` keeps both direct targets and adds menu controls
for address type (Random/Public/Auto) and security ordering (before/after service
discovery). Defaults: Random, security first. Options persist across BLE OFF/ON
and screen changes, but reset on reboot; no NVS writes are performed.

The same controls are available over serial, even with the Bluetooth menu open:
`CMD:BLE:OPTIONS:1:1` = Random/security first;
`CMD:BLE:OPTIONS:1:0` = Random/services first;
`CMD:BLE:OPTIONS:0:1` = Public/security first;
`CMD:BLE:OPTIONS:-1:1` = scanned/bond type or Public→Random fallback/security first.
Only address values -1/0/1 and security values 0/1 are accepted; options cannot
change during connection establishment or a fully connected session.
`CMD:BLE:STATUS` is read-only and now works while the Bluetooth UI is open.
Connect/disconnect/scan commands still require exiting the UI to avoid lifecycle
conflicts. Start the next connection from the on-device target menu.

Connection diagnostics enumerate primary service UUIDs, print link state and
last error before local teardown, and print disconnect reasons. NimBLE's last
error can be stale after successful operations, so it is not by itself proof
of a service-discovery failure. UI errors now distinguish security failure,
disconnection during service discovery, and HID service unavailable; none
asserts that the physical device is not HID. The SDK header and source changes
are both preserved in docs/patches/ble-connection-diagnostics.patch.

Reason for the change: direct5 connected to Nape Pro at the Random address,
then failed service lookup. The old error 7 was logged after disconnect and
could not establish the original lookup failure reason. Full pairing, service
subscription, and mouse input support are not yet validated.


### Direct7: initialization memory guard and UI cache release

Direct6 field observation: menu navigation left roughly 51 KB free with BLE
reported off. Repeated NimBLEDevice::init failures reduced free heap further
(to 19 KB, lifetime minimum about 5 KB). After reboot, the menu again left about
51 KB and returning home recovered only about 2 KB. Retained font decompression
buffers are a suspected contributor, not yet confirmed by a before/after test.

Direct7 uses short ASCII diagnostic option labels, clears renderer font caches
under the render lock before BLE init, and releases those caches after Bluetooth
screen rendering. It logs free heap before/after cache release. Existing keyboard
labels and runtime address/security options remain. This does not clear SD book
caches or erase pairing records.

Before attempting BLE init, direct mode requires 80000 bytes free (a conservative
threshold derived from measured initialization usage, not a universal NimBLE
requirement). Low-memory attempts are rejected without starting the controller.
If NimBLE init itself fails, subsequent attempts are blocked until reboot because
upstream init can retain partial resources while isInitialized is false.
This prevents repeated attempts from compounding the failure; it does not claim
to safely unwind an incompletely initialized controller. Physical validation of
the cache release and initialization recovery is pending.

### Direct8: Nape Pro security negotiation

Nape Pro testing showed that the X3 could occasionally complete the BLE link,
but security setup then failed. Direct8 enables LE Secure Connections and changes
the host IO capability from `DISPLAY_ONLY` to `NO_INPUT_OUTPUT`, matching the X3
hardware and allowing Just Works pairing when MITM is not required. This keeps
bonding enabled and does not add a passkey prompt. The image is intended for
Nape Pro validation; Clicks behavior is unchanged.

### Direct9 UI1: verified source restoration and current menus

Version `15.05-ble-direct9-ui1`, built with `pio run -e ble_direct_debug`.
The [recovered direct9 baseline](ble-baselines/direct9/README.md) contains the
historical SDK source, original binary hash, and successful serial excerpt.
The later successful test selected the scanned Nape address ending in `:91`
with Auto/security-first options and connected directly using type 1.

The previous attempted restoration had left `SC=false`, ignored some security
failures, and tried alternate address types even for known scan results. These
are now restored to the actual direct9 implementation. The connection worker,
security sequence, service discovery, subscriptions, and HID decoder match the
reference. This proves the source restoration, not the cause of every previous
radio failure or successful pairing on a newly flashed device.

Every `ble_direct_debug` build now runs
`python3 scripts/verify_ble_direct9_baseline.py` before compiling. It compares
12 connection/HID code blocks, nine BLE settings, timeout/default constants,
reference hashes, and the UI's shared Auto + security-first connection entry.
Changing SC, accepting failed security, or switching the UI to service-first
must fail that check. Update the documented baseline deliberately if connection
behavior is changed in a future build.

The screen has three actions: Bluetooth ON/OFF, Add device, and Saved keyboards.
Opening the screen observes the actual radio state. ON tries saved devices in
order; OFF calls the full SDK teardown. Leaving the screen preserves Bluetooth.
Saved devices are loaded from NVS even while OFF and can be deleted there.
Only explicit deletions queue bond removal for the next radio start; ordinary
connections never clear security keys. Retry targets the same address without
starting another search. Success is shown with a temporary popup.

UI support changes are separate from the frozen connection code: a static SDK
scan callback and locked result snapshots replace callbacks into a temporary
Activity; connection status includes worker completion and pending disconnect;
offline saved-list access and deferred deletion support the OFF menu. Selecting
Add device while already connected first tears down that single-client session
and then starts a fresh scan. Saved records remain intact.

This build preserves the direct9 input decoder. M1/M2-only page turning and
filtering all trackball/mouse reports are **not validated or claimed here**.
The previous guessed mouse-button-bit mapping has been removed with the other
post-direct9 backend changes.

#### Device checks after flashing

1. Confirm the device version is `15.05-ble-direct9-ui1`.
2. Open Bluetooth: after boot it should show OFF; the saved list should be
   readable without turning it on. No fixed device entries are injected.
3. Turn ON with Nape in the same pairing slot used in the successful test.
   Saved peers are tried in order. For its current address, use Add device and
   select the scan result. Expected log: `type=-1 securityFirst=1`, security
   `ok=1`, HID service `1812`, and `input subscription complete`.
4. After success, Back returns to the menu and exiting keeps Bluetooth ON.
   Reopen the menu and turn OFF to release the radio memory.
5. If an attempt fails, Retry must connect to the same peer without scanning.

Build, source comparisons, and image validation are automated. Pairing, popup
appearance on the e-paper screen, and OFF/ON behavior still need this device
check on the newly built binary.
