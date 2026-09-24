# Fix esp_lvgl_port vs ESP-IDF 6.0.2:
# on_frame_buf_complete does not exist; use on_refresh_done instead.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $root "managed_components"))) {
    $root = Get-Location
}
$file = Join-Path $root "managed_components\espressif__esp_lvgl_port\src\lvgl9\esp_lvgl_port_disp.c"
if (-not (Test-Path $file)) {
    Write-Host "Component not downloaded yet. Run: idf.py reconfigure"
    Write-Host "Then re-run this script."
    exit 1
}
$text = Get-Content -Raw $file
if ($text -match 'on_frame_buf_complete') {
    $text = $text -replace 'cbs\.on_frame_buf_complete\s*=\s*lvgl_port_flush_dpi_vsync_ready_callback;', 'cbs.on_refresh_done = lvgl_port_flush_dpi_vsync_ready_callback;'
    Set-Content -Path $file -Value $text -NoNewline
    Write-Host "Patched: $file"
} else {
    Write-Host "Already OK (no on_frame_buf_complete): $file"
}
