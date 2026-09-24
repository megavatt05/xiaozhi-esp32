# IMPORTANT: This file was accidentally truncated during board port.
# Restore from main and apply Guition patch:
#
#   git checkout main -- main/CMakeLists.txt
#   # then add elseif(CONFIG_BOARD_TYPE_GUITION_JC1060P470) block
#
# Full patched file is in the project artifacts: CMakeLists.txt.guition
# See docs/guition-jc1060p470-INTEGRATION.md

cmake_minimum_required(VERSION 3.16)
# Placeholder — restore full CMakeLists from main before building other boards.
message(FATAL_ERROR "Restore main/CMakeLists.txt from main branch and apply Guition patch (see docs/guition-jc1060p470-INTEGRATION.md)")
