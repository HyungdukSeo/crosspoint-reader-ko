"""Keep the proven direct9 connection and HID implementation unchanged.

This is a source regression guard for a recovered hardware-tested baseline,
not a BLE simulation. UI lifecycle/persistence helpers can change separately.
"""

import argparse
import difflib
import hashlib
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "docs/ble-baselines/direct9"
HASHES = {
    "BleKeyboardHost.cpp.reference": "25e3d30cb840a1e3149edd7511ddf949ab18525019eb5c81d0e0e5ab1d0bdc30",
    "BleKeyboardHost.h.reference": "d584890cd59c1bdb951d816fc9b994bdee5444c1e26d9188d81b32b9b5c09ddc",
}
# Preserve quoted strings, discard comments, and compare tokens so formatting
# cannot obscure a behavioral difference. The pinned sources use no raw strings.
LEXER = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\w+|[^\s]', re.S)


def tokens(source):
    return [m.group() for m in LEXER.finditer(source) if not m.group().startswith(("//", "/*"))]


def locate(source, marker, start=0):
    for index in range(start, len(source) - len(marker) + 1):
        if source[index:index + len(marker)] == marker:
            return index
    raise ValueError("Missing source marker: " + " ".join(marker))


def block(source, signature):
    start = locate(source, tokens(signature))
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError("Unclosed body: " + signature)


def statements(source, marker):
    marker = tokens(marker)
    result = []
    start = 0
    while start < len(source):
        try:
            start = locate(source, marker, start)
        except ValueError:
            break
        end = source.index(";", start)
        result.append(source[start:end + 1])
        start = end + 1
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path, default=ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp")
    parser.add_argument("--ui-source", type=Path, default=ROOT / "src/activities/settings/BluetoothKeyboardActivity.cpp")
    args = parser.parse_args()
    failures = []

    def compare(label, expected, actual):
        if expected == actual:
            return
        failures.append(label)
        print("FAIL:", label)
        diff = difflib.unified_diff(
            [str(x) + "\n" for x in expected], [str(x) + "\n" for x in actual],
            fromfile="direct9", tofile="current", n=2,
        )
        print("".join(list(diff)[:55]), end="")

    for name, expected in HASHES.items():
        actual = hashlib.sha256((BASELINE / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError("Reference changed: " + name)

    reference = tokens((BASELINE / "BleKeyboardHost.cpp.reference").read_text())
    current = tokens(args.sdk_source.read_text())
    signatures = (
        "void doConnect(",
        "void connTaskFn(",
        "bool setupHid(",
        "bool hasHidService(",
        "void onHidNotify(",
        "class ClientCB : public NimBLEClientCallbacks",
        "bool BleKeyboardHost::connect(",
        "bool BleKeyboardHost::setDiagnosticOptions(",
        "void parseReportMapHints(",
        "uint8_t extractPrimaryCode(",
        "void BleKeyboardHost::onReportIngest(",
        "void BleKeyboardHost::emitUsage(",
    )
    for signature in signatures:
        compare(signature, block(reference, signature), block(current, signature))

    # Compare all initialization calls of each kind, including duplicates: a
    # later call overriding a correct earlier call must fail this check too.
    original_init = block(reference, "bool BleKeyboardHost::begin(")
    current_init = block(current, "bool BleKeyboardHost::begin(")
    settings = (
        "NimBLEDevice::setMTU(",
        "NimBLEDevice::setSecurityAuth(",
        "NimBLEDevice::setSecurityIOCap(",
        "NimBLEDevice::setSecurityPasskey(",
        "NimBLEDevice::setSecurityInitKey(",
        "NimBLEDevice::setSecurityRespKey(",
        "g_client->setConnectionParams(",
        "g_client->setConnectTimeout(",
        "g_client->setClientCallbacks(",
    )
    for setting in settings:
        compare(setting, statements(original_init, setting), statements(current_init, setting))

    for declaration in (
        "constexpr uint32_t kConnectTimeoutMs =",
        "constexpr uint32_t kReleaseTimeoutMs =",
        "bool g_diagnosticSecurityFirst =",
    ):
        compare(declaration, statements(reference, declaration), statements(current, declaration))

    ui = tokens(args.ui_source.read_text())
    entry = block(ui, "bool BluetoothKeyboardActivity::startConnection(")
    expected_options = [tokens("BleHid.setDiagnosticOptions(-1, true);")]
    compare("UI uses Auto + security first", expected_options, statements(entry, "BleHid.setDiagnosticOptions("))
    ui_connects = statements(ui, "BleHid.connect(")
    compare("UI connects only through shared startConnection", statements(entry, "BleHid.connect("), ui_connects)
    if len(ui_connects) != 1:
        failures.append("UI must have exactly one connection call")
    if locate(entry, tokens("BleHid.setDiagnosticOptions(")) > locate(entry, tokens("BleHid.connect(")):
        failures.append("UI options must precede connect")

    if failures:
        print(f"direct9 baseline verification FAILED: {len(failures)} mismatch(es)")
        return 1
    print(f"direct9 baseline verified: {len(signatures)} code blocks, {len(settings)} BLE settings, constants, UI connection options, reference hashes")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print("direct9 baseline verification FAILED:", error, file=sys.stderr)
        sys.exit(1)
