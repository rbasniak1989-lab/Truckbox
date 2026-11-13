#!/usr/bin/env python3
"""Varredura aditiva: reporta somente arquivo/linha/tipo, nunca o valor encontrado.
Não certifica histórico Git ou segurança de dependências. Bibliotecas/binários não são fontes.
"""
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = {
    "private-key": rb"-----BEGIN [A-Z ]*PRIVATE KEY-----",
    "jwt": rb"eyJ[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{12,}",
    "supabase-secret": rb"sb_secret_[A-Za-z0-9_-]{16,}",
    "github-token": rb"gh[pousr]_[A-Za-z0-9_]{25,}",
    "credential-query": rb"https?://[^\s\"'<>]+[?&](?:key|token|api_key|secret)=[A-Za-z0-9_-]{12,}",
    "legacy-read-credential": rb"tbx-public-ro-[A-Za-z0-9_-]+",
}

def findings(data, name):
    results = []
    for kind, pattern in PATTERNS.items():
        for match in re.finditer(pattern, data):
            results.append(f"{name}:{data[:match.start()].count(bytes([10]))+1}: {kind}")
    return results

def main():
    failures = []
    count = 0
    for directory in (ROOT / "multimediaNext" / "src", ROOT / "backend-proposal"):
        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix in {".ttf", ".png"}:
                continue
            count += 1
            failures.extend(findings(path.read_bytes(), path.relative_to(ROOT)))
    for apk in map(Path, sys.argv[1:]):
        with zipfile.ZipFile(apk) as archive:
            for name in archive.namelist():
                if name.endswith((".dex", ".xml", ".json")):
                    failures.extend(findings(archive.read(name), f"{apk.name}!{name}"))
                if "homolog" in apk.name.lower() and name.endswith(".dex") and b"PreviewData" in archive.read(name):
                    failures.append(f"{apk.name}!{name}: PreviewData presente em homolog")
    print(f"Fontes novas verificadas: {count}; ocorrências: {len(failures)}")
    print("\n".join(failures))
    return bool(failures)

if __name__ == "__main__":
    sys.exit(main())
