"""Invalidate probe objects when the forced-include configuration changes.

SCons' source scanner does not track this -include header automatically.
"""

import hashlib
from pathlib import Path

Import("env")

config = Path(env["PROJECT_DIR"]) / "config" / "ble_probe_config.h"
digest = hashlib.sha256(config.read_bytes()).hexdigest()[:8]
env.Append(CPPDEFINES=[("CP_BLE_PROBE_CONFIG_HASH", "0x" + digest)])

# lib_ignore=BLE removes the controller too on this platform. Filter only the
# unused Arduino BLE wrapper sources, preserving esp32-hal-bt.c and libbt.a.
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
bundled_ble = (framework / "libraries" / "BLE" / "src").resolve()


def skip_bundled_ble(node):
    source = Path(node.srcnode().get_abspath()).resolve()
    return None if source.is_relative_to(bundled_ble) else node


env.AddBuildMiddleware(skip_bundled_ble)
