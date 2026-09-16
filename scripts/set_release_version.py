"""Set the firmware version from the release tag without rewriting INI comments."""

import os
import re
from pathlib import Path

version = os.environ["RELEASE_VERSION"]
if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-ko\.[0-9]+)?", version):
    raise SystemExit(f"Unsupported release version: {version!r}")
path = Path(__file__).resolve().parents[1] / "platformio.ini"
text, count = re.subn(
    r"(\[crosspoint\]\s*\nversion\s*=\s*)[^\n]+", lambda m: m[1] + version, path.read_text()
)
if count != 1:
    raise SystemExit("Expected exactly one [crosspoint] version setting")
path.write_text(text)

config_path = Path(__file__).resolve().parents[1] / "config" / "ble_probe_config.h"
config_text, config_count = re.subn(
    r'(#if defined\(CP_BLE_DIRECT_CONNECT\)\s*\n#define CROSSPOINT_VERSION ")[^"]+(")',
    lambda m: m[1] + version + m[2],
    config_path.read_text(),
)
if config_count != 1:
    raise SystemExit("Expected exactly one CP_BLE_DIRECT_CONNECT version setting")
config_path.write_text(config_text)

print(f"Firmware version: {version}")
