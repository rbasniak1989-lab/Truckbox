#!/usr/bin/env bash
set -euo pipefail

# Activate ESP-IDF first: . /path/to/esp-idf-v5.5.2/export.sh
# This script compiles only. It never flashes a connected device.
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
requested_role=${1:-all}
case "$requested_role" in
  core|link) roles=("$requested_role") ;;
  all) roles=(core link) ;;
  *) printf 'Uso: %s [core|link|all]\n' "$0" >&2; exit 2 ;;
esac
if ! command -v idf.py >/dev/null 2>&1; then
  printf 'Ative ESP-IDF v5.5.2 com export.sh antes de compilar.\n' >&2
  exit 2
fi
idf_version=$(idf.py --version)
if [[ "$idf_version" != 'ESP-IDF v5.5.2' ]]; then
  printf 'ESP-IDF v5.5.2 obrigatorio; encontrado: %s\n' "$idf_version" >&2
  exit 2
fi
for build_role in "${roles[@]}"; do
  project_version=0.2.13
  idf.py -C "$project_root" -B "$project_root/build-$build_role" \
    -DTRUCKBOX_ROLE="$build_role" -DPROJECT_VER="$project_version" build
done
