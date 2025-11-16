"""
Build script for RTSP Server component
Adds ESP-IDF H.264 hardware encoder libraries
"""

Import("env")

# Add ESP-IDF libraries for H.264 hardware encoder (ESP32-P4)
# The hardware encoder functions (esp_h264_enc_hw_new, etc.) are provided
# by ESP-IDF driver component

# Add ESP-IDF library paths and libraries
env.Append(
    LIBS=[
        "driver",           # ESP-IDF driver component (contains H.264 HW encoder)
        "esp_driver_h264",  # ESP32-P4 H.264 hardware encoder driver
        "hal",              # Hardware Abstraction Layer
    ]
)

print("[RTSP Server] Added ESP-IDF H.264 hardware encoder libraries")
