#!/usr/bin/env bash
# Fix esp_lvgl_port for ESP-IDF 6.0.2 DPI callbacks
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FILE="$ROOT/managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c"
if [[ ! -f "$FILE" ]]; then
  echo "Component missing. Run idf.py reconfigure first."
  exit 1
fi
if grep -q 'on_frame_buf_complete' "$FILE"; then
  sed -i 's/cbs\.on_frame_buf_complete = lvgl_port_flush_dpi_vsync_ready_callback;/cbs.on_refresh_done = lvgl_port_flush_dpi_vsync_ready_callback;/g' "$FILE"
  echo "Patched: $FILE"
else
  echo "Already OK: $FILE"
fi
