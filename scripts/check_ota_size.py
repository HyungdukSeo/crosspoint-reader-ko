"""Enforce the stock OTA partition limit on the actual downloadable image."""

import sys
from pathlib import Path

LIMIT = 0x640000
path = Path(sys.argv[1])
size = path.stat().st_size
if size == 0 or size > LIMIT:
    raise SystemExit(f"OTA image size invalid: {size} bytes (limit {LIMIT})")
print(f"OTA image: {size} bytes; headroom: {LIMIT - size} bytes")
