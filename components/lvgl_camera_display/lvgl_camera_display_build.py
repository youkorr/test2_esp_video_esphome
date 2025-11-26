"""
Script de build PlatformIO pour LVGL Camera Display
Note: ESP-DL detection is DISABLED due to external dependencies

ESP-DL and detection components require:
- esp_dsp.h (ESP-DSP library) - not available in ESPHome
- Multiple ESP-IDF specific libraries
- API compatibility fixes for DetectWrapper::load_model()

Include paths are already added via __init__.py
No sources are compiled by this script to avoid build errors.
"""

import os
Import("env")

print("[LVGL Camera Display Build] Script running...")
print("[LVGL Camera Display Build] ESP-DL detection is temporarily disabled")
print("[LVGL Camera Display Build] Reason: Missing external dependencies (esp_dsp.h, etc.)")
print("[LVGL Camera Display Build] Include paths added via __init__.py")
print("[LVGL Camera Display Build] No additional compilation needed")
