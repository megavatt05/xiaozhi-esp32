#!/usr/bin/env bash
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT/main/boards/guition-jc1060p470/full-restore"
echo "Reassembling Kconfig.projbuild..."
( cd Kconfig.projbuild && cat part00.txt part01.txt part02.txt > "$ROOT/main/Kconfig.projbuild" )
echo "Reassembling CMakeLists.txt..."
( cd CMakeLists.txt && cat part00.txt part01.txt part02.txt part03.txt part04.txt > "$ROOT/main/CMakeLists.txt" )
echo "Done. Verify GUITION:"
grep -n "GUITION_JC1060P470" "$ROOT/main/Kconfig.projbuild" "$ROOT/main/CMakeLists.txt" || true
