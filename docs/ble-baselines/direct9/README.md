# Verified direct9 source baseline

These reference files preserve the BLE SDK used by the successful
`15.05-ble-direct9` build. They were recovered from SDK commit `a485dc4` and the
actual source-edit tool records, rather than reconstructed from later summaries.

Evidence timestamps are UTC:

- 2026-09-15 08:23:49 / 08:24:16: connection diagnostics.
- 2026-09-15 09:20:12 / 09:28:58: 20-second timeout and diagnostic reconnect policy.
- 2026-09-15 09:41:39 / 10:58:04: selectable diagnostic settings and memory guard.
- 2026-09-15 11:24:59: enable Secure Connections (`SC=true`) and NoInputNoOutput.
- 2026-09-15 11:37:52: direct9 changes menu initialization only; SDK unchanged.
- 2026-09-15 11:40:34: direct9 binary copied and its SHA-256 recorded.
- 2026-09-16 03:24:01: serial read confirms the later successful direct9 test.

The recovered `doConnect()` also matches the complete source printed in the
2026-09-15 11:24:33 tool call. Original firmware size: **6,145,920 bytes**.

| Artifact | SHA-256 |
| --- | --- |
| Original `firmware-15.05-ble-direct9.bin` | `f7bfb4b7e65a6ad44cbccb41681a50eda2c5a60c4b6d9a53af0b3b6e87823aae` |
| `BleKeyboardHost.cpp.reference` | `25e3d30cb840a1e3149edd7511ddf949ab18525019eb5c81d0e0e5ab1d0bdc30` |
| `BleKeyboardHost.h.reference` | `d584890cd59c1bdb951d816fc9b994bdee5444c1e26d9188d81b32b9b5c09ddc` |

## Successful settings

The [captured serial excerpt](successful-connection.log) uses Auto address type
(`-1`) and security first (`true`). The scanned address `EA:22:99:9E:A8:91` has
type `1` and connects immediately, then encrypts, discovers five services, and
subscribes to HID input. The preceding fixed-address attempt failed; a type-0
failure followed by type-1 success is **not** the sequence in this successful test.

The baseline enables bonding and Secure Connections, leaves MITM disabled, uses
NoInputNoOutput, and distributes ENC keys. MTU is 23. Connection parameters are
12–24 interval units, zero latency, and 800 supervision-timeout units. It does
not delete bonds before connecting, continue after failed security, or fall back
to unencrypted HID. Alternate address type is attempted only for an unknown type.

## Regression check

```sh
python3 scripts/verify_ble_direct9_baseline.py
```

This checks the reference hashes, connection/security/HID code against these
snapshots, and the shared UI connection entry point's Auto/security-first
settings. Formatting and comments may differ; executable tokens must match.
UI lifecycle and saved-device helpers may be added outside the checked code.

The reference retains direct9's original HID decoder, including its known
limitation with mouse reports. It does not establish correct M1/M2-only input
filtering. This source comparison and a successful build do not replace a new
physical connection test. Preserve these references when changing later builds.
