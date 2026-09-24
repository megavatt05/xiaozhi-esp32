#!/usr/bin/env bash
# Restore full Kconfig/CMake from main and apply Guition patches
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
echo "Checking out full files from main..."
git checkout main -- main/Kconfig.projbuild main/CMakeLists.txt
echo "Applying Guition patches..."
patch -p1 < main/boards/guition-jc1060p470/patches/kconfig.guition.patch
patch -p1 < main/boards/guition-jc1060p470/patches/cmake.guition.patch
echo "Done. Verify:"
grep -n "GUITION_JC1060P470" main/Kconfig.projbuild main/CMakeLists.txt
