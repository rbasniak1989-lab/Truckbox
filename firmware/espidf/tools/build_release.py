#!/usr/bin/env python3
"""Package Core production builds, complete sibling sources, and manual recovery tools."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.2.17"
EXPECTED_OFFSETS = {0, 0x8000, 0xF000, 0x20000}
REQUIRED_SOURCE = (
    "firmware/espidf/CMakeLists.txt",
    "firmware/espidf/partitions.csv",
    "firmware/espidf/dependencies.lock",
    "firmware/espidf/components/tb_core/CMakeLists.txt",
    "firmware/j1708_17pid/src/tbx_j1587_decoder.c",
    "firmware/j1708_17pid/src/tbx_j1587_passive.c",
    "firmware/j1708_17pid/src/tbx_j1587_passive_full.c",
    "firmware/j1708_17pid/src/tbx_j1708_requests.c",
    "firmware/j1939/INVENTARIO_J1939_76_SINAIS.csv",
    "firmware/j1939/INVENTARIO_J1939_46_PGNS_OBSERVADAS.csv",
    "firmware/j1939/references/DOCUMENTO_MESTRE_J1939_v1.1_2026-08-11.md",
    "firmware/j1939/references/VALIDACAO_LOGS_MAE_PARSER_V1.md",
    "firmware/j1939/references/truckbox_j1939_parser_v1.py",
    "firmware/REV_B1_CODEX_IMPLEMENTATION.md",
)
EXCLUDED_PARTS = {"release", "releases", "managed_components", ".git", "__pycache__", "node_modules", "private", ".pytest_cache"}
EXCLUDED_SUFFIXES = {".log", ".zip", ".bin", ".elf", ".pyc", ".o", ".obj", ".a", ".apk", ".aab", ".jks", ".keystore"}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def descriptor(path: Path) -> tuple[str, str]:
    data = path.read_bytes()
    if len(data) < 288 or data[0] != 0xE9 or (data[3] >> 4) != 3:
        raise ValueError(f"Not an 8 MB ESP image: {path}")
    if struct.unpack_from("<H", data, 12)[0] != 13:
        raise ValueError(f"Not an ESP32-C6 image: {path}")
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError(f"Missing application descriptor: {path}")
    # app_desc contains the actual IDF version used, not a package label.
    idf_version = data[144:176].split(b"\0", 1)[0].decode("ascii")
    if idf_version != "v5.5.2":
        raise ValueError(f"Expected ESP-IDF v5.5.2, found {idf_version}: {path}")
    return (data[48:80].split(b"\0", 1)[0].decode("ascii"),
            data[80:112].split(b"\0", 1)[0].decode("ascii"))


def source_entries(project_root: Path) -> dict[str, Path]:
    firmware_root = project_root.parent
    entries = {}
    for directory in (project_root, firmware_root / "j1708_17pid", firmware_root / "j1939"):
        if not directory.is_dir():
            raise ValueError(f"Required source directory missing: {directory}")
        for path in sorted(directory.rglob("*")):
            rel = path.relative_to(firmware_root)
            if (not path.is_file() or path.is_symlink()
                    or any(p in EXCLUDED_PARTS or p.startswith("build-") or p == "build" for p in rel.parts)
                    or path.suffix.lower() in EXCLUDED_SUFFIXES
                    or path.name in {"sdkconfig", "sdkconfig.old", "release-summary.json", "SOURCE_PROVENANCE.json"}):
                continue
            if rel.parts[:3] == ("espidf", "tests", "web") and path.suffix in {".png", ".json"}:
                continue
            entries["firmware/" + rel.as_posix()] = path
    # App and read-only Cloud sources are part of the same incremental delivery.
    # Android needs its sibling module/settings; no APK/build or credential files.
    repository=firmware_root.parent
    for name in ("app","multimedia","cloud/diagnostics","supabase","gradle","scripts",".github/workflows"):
        directory=repository/name
        if not directory.is_dir():continue
        for path in sorted(directory.rglob("*")):
            rel=path.relative_to(repository)
            if not path.is_file() or path.is_symlink() or any(p in EXCLUDED_PARTS or p in {"build",".gradle"} or p.startswith("build-") for p in rel.parts):continue
            if path.suffix.lower() in EXCLUDED_SUFFIXES or path.name.startswith(".env") or path.name=="local.properties":continue
            entries[rel.as_posix()]=path
    for name in ("build.gradle.kts","settings.gradle.kts","gradle.properties","gradlew","gradlew.bat","README.md"):
        path=repository/name
        if path.is_file():entries[name]=path
    contract = firmware_root / "REV_B1_CODEX_IMPLEMENTATION.md"
    if contract.is_file():
        entries["firmware/" + contract.name] = contract
    missing = set(REQUIRED_SOURCE) - entries.keys()
    if missing:
        raise ValueError("Incomplete source archive: " + ", ".join(sorted(missing)))
    # Validate the exact relative CMake source paths as they will resolve after extraction.
    cmake = project_root / "components/tb_core/CMakeLists.txt"
    for reference in re.findall(r'"(\.\./[^"\s]+\.(?:c|h))"', cmake.read_text(encoding="utf-8")):
        resolved = (cmake.parent / reference).resolve()
        try:
            key = "firmware/" + resolved.relative_to(firmware_root.resolve()).as_posix()
        except ValueError as exc:
            raise ValueError(f"CMake source escapes firmware layout: {reference}") from exc
        if key not in entries:
            raise ValueError(f"CMake sibling source absent from archive: {reference}")
    return entries


def git_value(directory: Path, *args: str) -> str | None:
    try:
        return subprocess.check_output(["git", "-C", str(directory), *args], text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def provenance(project_root: Path, builds: dict) -> dict:
    previous = project_root / "SOURCE_PROVENANCE.json"
    result = json.loads(previous.read_text(encoding="utf-8")) if previous.is_file() else {}
    original = project_root.parent / "releases/v0.1.0-b1/truckbox-b1.zip"
    if original.is_file():
        with zipfile.ZipFile(original) as archive:
            source_sha = hashlib.sha256(archive.read("truckbox_firmware_source.zip")).hexdigest()
        result["original_release"] = {
            "branch": "firmware-revb1-ota-v0.1.0-b1",
            "commit": git_value(project_root, "rev-parse", "origin/firmware-revb1-ota-v0.1.0-b1"),
            "file": "firmware/releases/v0.1.0-b1/truckbox-b1.zip",
            "sha256": digest(original), "embedded_source_sha256": source_sha,
        }
    if not result.get("original_release", {}).get("embedded_source_sha256"):
        raise ValueError("Original release/source provenance missing")
    result.update({"version": VERSION, "state": "CANDIDATA_DIAGNOSTICO_AGUARDANDO_APROVACAO", "validation": "PARTIAL",
                   "physical_validation": "PENDING", "esp_idf": "v5.5.2"})
    result["development"] = {
        "branch": git_value(project_root, "branch", "--show-current"),
        "commit": git_value(project_root, "rev-parse", "HEAD"),
        "dirty": bool(git_value(project_root, "status", "--porcelain")),
        "integration_base_branch": "feature/revb1-v0216-auto-j1708",
        "integration_base_commit": git_value(project_root, "merge-base", "HEAD", "0bbc853"),
        "reference_material_commit": git_value(project_root, "rev-parse", "origin/feature/revb1-j1708-17-pids"),
    }
    result["builds"] = {}
    for role, build in builds.items():
        description = build["description"]
        idf_path = Path(description["idf_path"])
        result["builds"][role] = {
            "project": description["project_name"], "version": description["project_version"],
            "target": description["target"], "idf_revision": description["git_revision"],
            "esp_idf_commit": git_value(idf_path, "rev-parse", "HEAD"),
            "compiler": Path(description["c_compiler"]).name,
        }
    return result


def validate_build(project_root: Path, role: str) -> dict:
    build = project_root / f"build-{role}"
    description = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
    if (description["project_name"] != f"truckbox_{role}" or description["target"] != "esp32c6"
            or description["project_version"] != VERSION or description["git_revision"] != "v5.5.2"):
        raise ValueError(f"Build metadata incompatible with v{VERSION}: {role}")
    args = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    if args["flash_settings"] != {"flash_mode": "dio", "flash_size": "8MB", "flash_freq": "80m"}:
        raise ValueError(f"Unexpected physical flash settings: {role}")
    files = args["flash_files"]
    if len(files) != 4 or {int(offset, 0) for offset in files} != EXPECTED_OFFSETS:
        raise ValueError(f"Unexpected flash regions; NVS/storage must be preserved: {role}")
    checked = []
    for offset, source in files.items():
        path = (build / source).resolve()
        if build.resolve() not in path.parents:
            raise ValueError(f"Build artifact escapes build directory: {source}")
        start, length = int(offset, 0), path.stat().st_size
        if length <= 0 or start + length > {0: 0x8000, 0x8000: 0x9000, 0xF000: 0x11000, 0x20000: 0x320000}[start]:
            raise ValueError(f"Artifact overlaps protected region: {source}")
        if start == 0x20000 and descriptor(path) != (VERSION, f"truckbox_{role}"):
            raise ValueError(f"Wrong application role/version: {role}")
        checked.append((offset, path))
    return {"description": description, "args": args, "files": checked,
            "app": build / f"truckbox_{role}.bin", "build": build}


def write_windows_tools(destination: Path, roles=("core", "link")) -> None:
    for role in roles:
        command = (
            '@echo off\r\ncd /d "%~dp0"\r\nif "%~1"=="" (\r\n'
            f'  echo Uso: flash_{role}.cmd COM3\r\n  pause\r\n  exit /b 2\r\n)\r\n'
            f'py "%~dp0tools\\flash_device.py" --role {role} --port "%~1"\r\n'
            'set TB_FLASH_RESULT=%ERRORLEVEL%\r\npause\r\nexit /b %TB_FLASH_RESULT%\r\n'
        )
        (destination / f"flash_{role}.cmd").write_bytes(command.encode("ascii"))
    (destination / "monitor.cmd").write_bytes((
        '@echo off\r\ncd /d "%~dp0"\r\nif "%~1"=="" (\r\n'
        '  echo Uso: monitor.cmd COM3\r\n  pause\r\n  exit /b 2\r\n)\r\n'
        'py "%~dp0tools\\monitor_device.py" "%~1"\r\n'
    ).encode("ascii"))


def package_release(project_root: Path, output_dir: Path, package_path: Path, roles=("core", "link")) -> dict:
    project_root, output_dir, package_path = project_root.resolve(), output_dir.resolve(), package_path.resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise ValueError(f"Output directory is not empty: {output_dir}; choose a fresh directory")
    if package_path == output_dir or output_dir in package_path.parents:
        raise ValueError("Package ZIP must be outside its release directory")
    entries = source_entries(project_root)
    builds = {role: validate_build(project_root, role) for role in roles}
    origin = provenance(project_root, builds)
    manifest = {"hardware_revision": "B1", "version": VERSION, "state": "CANDIDATA_DIAGNOSTICO_AGUARDANDO_APROVACAO",
                "validation": "PARTIAL", "physical_validation": "pending", "esp_idf": "v5.5.2",
                "preserves_nvs_and_storage": True, "automatic_flash": False, "images": {}}
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    package_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="truckbox-v020-", dir=output_dir.parent) as scratch:
        staging = Path(scratch) / "release"
        staging.mkdir()
        for role, build in builds.items():
            destination = staging / "images" / role
            destination.mkdir(parents=True)
            files = []
            for offset, source in build["files"]:
                target = destination / source.name
                shutil.copy2(source, target)
                files.append({"offset": offset, "file": target.relative_to(staging).as_posix(),
                              "sha256": digest(target), "size": target.stat().st_size})
            ota = staging / f"truckbox_{role}_{VERSION}_ota.bin"
            shutil.copy2(build["app"], ota)
            manifest["images"][role] = {
                "project": f"truckbox_{role}", "flash_size": "8MB", "flash_mode": "dio", "flash_freq": "80m",
                "usb_files": files, "ota_file": ota.name, "ota_sha256": digest(ota), "ota_size": ota.stat().st_size,
            }
            # Preserve symbols for decoding watchdog/backtrace addresses of
            # this exact OTA build. They are never part of a USB flash list.
            symbols=[]
            for suffix in ("elf","map"):
                compiled=build["build"] / f"truckbox_{role}.{suffix}"
                if compiled.is_file():
                    symbol_dir=staging / "symbols" / role
                    symbol_dir.mkdir(parents=True,exist_ok=True)
                    target=symbol_dir / compiled.name;shutil.copy2(compiled,target)
                    symbols.append({"file":target.relative_to(staging).as_posix(),"sha256":digest(target)})
            manifest["images"][role]["debug_symbols"]=symbols
            # Reproducible build inputs and actual offsets, no absolute local paths.
            metadata_dir = staging / "build_metadata" / role
            metadata_dir.mkdir(parents=True)
            shutil.copy2(build["build"] / "sdkconfig", metadata_dir / "sdkconfig")
            shutil.copy2(build["build"] / "flasher_args.json", metadata_dir / "flasher_args.json")
        for path in sorted(project_root.glob("*.md")):
            shutil.copy2(path, staging / path.name)
        shutil.copy2(project_root / "partitions.csv", staging / "partitions.csv")
        for directory in ("cloud", "sensors", "test_evidence"):
            if (project_root / directory).is_dir():
                shutil.copytree(project_root / directory, staging / directory,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        template = project_root / "j1708_request_config.template.json"
        if template.is_file(): shutil.copy2(template, staging / template.name)
        shutil.copytree(project_root / "licenses", staging / "licenses")
        (staging / "tools").mkdir()
        for name in ("flash_device.py", "monitor_device.py"):
            shutil.copy2(project_root / "tools" / name, staging / "tools" / name)
        write_windows_tools(staging, roles)
        write_json(staging / "SOURCE_PROVENANCE.json", origin)
        source_zip = staging / "truckbox_firmware_source.zip"
        with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, path in entries.items():
                archive.write(path, name)
            archive.writestr("firmware/espidf/SOURCE_PROVENANCE.json", json.dumps(origin, ensure_ascii=False, indent=2) + "\n")
        manifest["source"] = {"file": source_zip.name, "sha256": digest(source_zip), "layout": "firmware/espidf + firmware/j1708_17pid + firmware/j1939"}
        write_json(staging / "manifest.json", manifest)
        sums = []
        for path in sorted(staging.rglob("*")):
            if path.is_file():
                sums.append(f"{digest(path)}  {path.relative_to(staging).as_posix()}")
        (staging / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")
        temporary_zip = Path(scratch) / "package.zip"
        with zipfile.ZipFile(temporary_zip, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(staging.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(staging))
        # All validations/copies completed before publishing either artifact.
        if output_dir.exists():
            output_dir.rmdir()
        staging.rename(output_dir)
        temporary_zip.replace(package_path)
    return {"package": str(package_path), "release_directory": str(output_dir), "bytes": package_path.stat().st_size,
            "sha256": digest(package_path), "version": VERSION, "images": manifest["images"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "release")
    parser.add_argument("--package", type=Path, default=ROOT / f"Truckbox_B1_firmware_{VERSION}_TESTES.zip")
    parser.add_argument("--core-only", action="store_true", help="Package only Core; keep installed Link firmware")
    args = parser.parse_args()
    print(json.dumps(package_release(ROOT, args.output_dir, args.package,
        roles=("core",) if args.core_only else ("core", "link")), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
