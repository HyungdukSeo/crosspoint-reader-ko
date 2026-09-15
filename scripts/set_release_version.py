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
print(f"Firmware version: {version}")
