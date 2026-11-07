#!/usr/bin/env python3
"""First installation over USB. Verifies files and flash size; preserves NVS/storage."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import re
import struct
import subprocess
import sys
from pathlib import Path


def checked_file(root: Path, item: dict) -> Path:
    path = (root / item["file"]).resolve()
    if root.resolve() not in path.parents:
        raise ValueError("Caminho fora do pacote")
    if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
        raise ValueError(f"Arquivo alterado ou incompleto: {path.name}")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=("core", "link"), required=True)
    parser.add_argument("--port", required=True, help="Porta mostrada no Gerenciador de Dispositivos")
    args = parser.parse_args()
    if not re.fullmatch(r"COM[1-9][0-9]{0,4}", args.port, re.IGNORECASE):
        parser.error("Use uma porta como COM3")
    try:
        version = importlib.metadata.version("esptool")
    except importlib.metadata.PackageNotFoundError:
        print("Instale a ferramenta: py -m pip install --upgrade esptool", file=sys.stderr)
        return 2
    if int(version.split(".")[0]) != 5:
        print('Este pacote usa esptool 5.x. Instale: py -m pip install "esptool>=5,<6"', file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    image = manifest["images"][args.role]
    if image["project"] != f"truckbox_{args.role}" or image["flash_size"] != "8MB":
        raise ValueError("Manifesto incompatível")
    files = [(item, checked_file(root, item)) for item in image["usb_files"]]
    offsets = {int(item["offset"], 0) for item, _ in files}
    if offsets != {0, 0x8000, 0xF000, 0x20000} or len(files) != 4:
        raise ValueError("O pacote não contém as quatro regiões esperadas")
    # Keep user configuration and the entire telemetry filesystem intact.
    for item, path in files:
        start = int(item["offset"], 0)
        end = start + path.stat().st_size
        if (start < 0xF000 and end > 0x9000) or end > 0x320000:
            raise ValueError(f"Região USB inesperada: {path.name}")
        if start == 0x20000:
            data = path.read_bytes()
            if (len(data) < 288 or data[0] != 0xE9 or data[3] >> 4 != 3
                    or struct.unpack_from("<H", data, 12)[0] != 13
                    or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432
                    or data[80:112].split(b"\0", 1)[0] != f"truckbox_{args.role}".encode()):
                raise ValueError("Imagem de aplicação incompatível com o papel selecionado")
    base = [sys.executable, "-m", "esptool", "--chip", "esp32c6",
            "--port", args.port.upper(), "--baud", "115200",
            "--before", "no-reset", "--after", "no-reset"]
    print(f"Truckbox B1: instalação USB do {args.role.upper()}, versão {manifest['version']}.", flush=True)
    print("BOOT/GPIO9 deve estar no GND, GPIO8 alto e o ESP32 alimentado.", flush=True)
    probe = subprocess.run(base + ["--no-stub", "flash-id"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(probe.stdout, flush=True)
    if probe.returncode or not re.search(r"Detected flash size:\s*8\s*MB\b", probe.stdout, re.IGNORECASE):
        print("Não foi confirmada flash de 8 MB. Nada foi gravado.", file=sys.stderr)
        return 3
    command = base + ["write-flash", "--flash-mode", image["flash_mode"],
                      "--flash-freq", image["flash_freq"], "--flash-size", "8MB"]
    for item, path in files:
        command.extend((item["offset"], str(path)))
    subprocess.run(command, check=True)
    print("\nGravação concluída e verificada pelo esptool.")
    print("1. Desconecte USB e desligue a fonte.")
    print("2. Retire SOMENTE o jumper BOOT/GPIO9-GND; mantenha o resistor do GPIO8.")
    print("3. Ligue a fonte e reconecte o USB.")
    print(f"4. Execute monitor.cmd {args.port.upper()} e digite setup seguido de Enter.")
    print("As próximas atualizações usam apenas o arquivo OTA .bin pela página Wi-Fi.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"Falha: {exc}", file=sys.stderr)
        raise SystemExit(1)
