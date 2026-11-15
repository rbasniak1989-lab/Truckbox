#!/usr/bin/env python3
"""Audit the published Core archive and OTA bytes, independently of a fresh CI build."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile

from build_release import CORE_NETWORK_BUILD_CONTRACT, ROLLBACK_CORE_SHA256, VERSION


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def verify(package: Path, ota: Path) -> dict:
    data = ota.read_bytes()
    assert data[0] == 0xE9 and data[3] >> 4 == 3, "Expected 8 MB ESP image"
    assert struct.unpack_from("<H", data, 12)[0] == 13, "Expected ESP32-C6"
    for start, end, expected in ((48, 80, VERSION), (80, 112, "truckbox_core"), (144, 176, "v5.5.2")):
        assert data[start:end].split(b"\0", 1)[0].decode() == expected, "Wrong image identity"
    assert len(data) < 0x300000 and CORE_NETWORK_BUILD_CONTRACT in data, "Compiled sharing contract absent"
    with zipfile.ZipFile(package) as z:
        assert z.testzip() is None, "ZIP CRC failure"
        for line in z.read("SHA256SUMS.txt").decode().splitlines():
            digest, name = line.split("  ", 1)
            assert sha(z.read(name)) == digest, f"Archive checksum mismatch: {name}"
        manifest = json.loads(z.read("manifest.json"))
        assert manifest["version"] == VERSION and set(manifest["images"]) == {"core"}
        assert manifest["preserves_nvs_and_storage"] and not manifest["automatic_flash"]
        image = manifest["images"]["core"]
        assert z.read(image["ota_file"]) == data, "Standalone OTA differs from packaged OTA"
        assert image["ota_sha256"] == sha(data)
        effective = set(z.read("build_metadata/core/sdkconfig").decode().splitlines())
        assert {"CONFIG_TB_J1708_ONLY=y", "CONFIG_LWIP_IP_FORWARD=y", "CONFIG_LWIP_IPV4_NAPT=y"} <= effective
        assert "CONFIG_TB_RAW_TCP=y" not in effective and "CONFIG_LWIP_IPV4_NAPT_PORTMAP=y" not in effective
        assert sha(z.read("partitions.csv")) == "5e25baf13060f871fb6e9103f6cec4000a6c0e36bdf4d554fa037a26f3f91832"
        rollback = manifest["rollback"]["core"]
        assert sha(z.read(rollback["file"])) == ROLLBACK_CORE_SHA256 == rollback["sha256"]
        provenance = json.loads(z.read("SOURCE_PROVENANCE.json"))
        assert not provenance["development"]["dirty"], "Source tree was dirty when packaged"
        source = manifest["source"]
        assert sha(z.read(source["file"])) == source["sha256"]
        with zipfile.ZipFile(io.BytesIO(z.read(source["file"]))) as src:
            assert json.loads(src.read("firmware/espidf/SOURCE_PROVENANCE.json")) == provenance
            assert f'set(PROJECT_VER "{VERSION}" ' in src.read("firmware/espidf/CMakeLists.txt").decode()
            assert b"/api/live" in src.read("firmware/espidf/components/tb_web/tb_web.c")
            assert "multimedia/src/main/java/br/com/truckbox/multimedia/LiveRepository.kt" in src.namelist()
        return {"version": VERSION, "ota_bytes": len(data), "ota_sha256": sha(data),
                "package_sha256": sha(package.read_bytes()), "source_commit": provenance["development"]["commit"],
                "effective_forwarding_napt": True, "raw_tcp_disabled": True,
                "partition_table_preserved": True, "rollback_verified": True,
                "shipped_bytes_audited": True, "physical_internet_test": "PENDING"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("ota", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.package, args.ota), ensure_ascii=False, indent=2))
