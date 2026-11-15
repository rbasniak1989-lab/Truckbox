#!/usr/bin/env python3
"""Check production sources and optionally the Motorista APK; never print secrets."""
import base64
import json
import re
import sys
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
forbidden = [
    b"192.168.4.1", b"truckbox.local", b"/api/gateway/", b"/api/state", b"/api/gps",
    b"Lbr/com/truckbox/app/transport/", b"Lbr/com/truckbox/app/gateway/CoreGatewayClient;",
    b"Lbr/com/truckbox/app/repository/TruckRepository;",
]


def inspect(data: bytes, name: str) -> None:
    for value in forbidden:
        if value in data:
            raise SystemExit(f"Forbidden local Core reference in {name}")
    if re.search(rb"tbxd1_[A-Za-z0-9_-]{100,}", data):
        raise SystemExit(f"Private activation code in {name}")
    if re.search(rb"sb_secret_[A-Za-z0-9_-]{10,}", data):
        raise SystemExit(f"Server secret in {name}")
    for token in re.findall(rb"eyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+", data):
        try:
            middle = token.split(b".")[1]
            claims = json.loads(base64.urlsafe_b64decode(middle + b"=" * (-len(middle) % 4)))
        except (ValueError, UnicodeDecodeError):
            continue
        if claims.get("role") == "service_role":
            raise SystemExit(f"Service-role credential in {name}")


for path in (root / "app/src/main").rglob("*.kt"):
    inspect(path.read_bytes(), str(path.relative_to(root)))
if len(sys.argv) > 1:
    with zipfile.ZipFile(sys.argv[1]) as apk:
        for name in apk.namelist():
            if name.endswith(".dex") or name.startswith("assets/"):
                inspect(apk.read(name), name)
print("Motorista Cloud checks passed: no local Core client or embedded activation/server credential")
