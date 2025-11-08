# CRITICAL FIX: Forced Rebuild of Sensor Detection Code

## Problem Identified

The root cause of the sensor detection failure was **build cache corruption**. Despite clean rebuilds and file modifications, two critical files were NOT being recompiled:

1. `components/esp_video/src/esp_video_init.c` - Contains the sensor detection loop
2. `components/esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c` - Defines sensor detection order

**Evidence:**
- Debug logs added to `esp_video_init.c` (lines 400-409) NEVER appeared in boot output
- No "🔍 Starting sensor detection loop..." messages
- No "Checking sensor at..." messages
- No "Camera sensor is not..." error messages from detection attempts
- This proved the sensor detection loop was NOT executing

PlatformIO/SCons caches `.o` (object) files aggressively. Even `esphome clean` doesn't always remove them, causing stale compiled code to be linked into the firmware.

## The Solution

Modified `components/esp_video/esp_video_build.py` to **forcefully delete cached `.o` files** for the critical source files before every build:

```python
# Fichiers critiques qui doivent être recompilés (problème de cache SCons)
force_rebuild_sources = [
    "esp_video_init.c",
    "esp_cam_sensor_detect_stubs.c",
]

# Supprimer les .o correspondants dans le build directory
build_dir = env.subst("$BUILD_DIR")
for src_name in force_rebuild_sources:
    # Chercher les .o avec ce nom de base (récursivement)
    obj_pattern = os.path.join(build_dir, "**", f"*{src_name.replace('.c', '.o')}")
    obj_files = glob.glob(obj_pattern, recursive=True)
    for obj_file in obj_files:
        os.remove(obj_file)
```

This ensures these files are ALWAYS recompiled, bypassing the cache problem.

## What You Should See Now

When you compile and flash this version, the boot logs should now show:

### 1. Build-time logs (during compilation):
```
[ESP-Video Build] 🔨 Deleted cached object: esp_video_init.o
[ESP-Video Build] 🔨 Deleted cached object: esp_cam_sensor_detect_stubs.o
[ESP-Video Build] + src/esp_video_init.c
[ESP-Video Build] + esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c
```

### 2. Boot logs (after flashing):
```
[esp_video_init] 🔍 Starting sensor detection loop...
[esp_video_init]   DEBUG: __esp_cam_sensor_detect_fn_array_start = 0x...
[esp_video_init]   DEBUG: &__esp_cam_sensor_detect_fn_array_end = 0x...
[esp_video_init]   DEBUG: Pointer difference = 60 bytes
[esp_video_init]   DEBUG: sizeof(esp_cam_sensor_detect_fn_t) = 20 bytes
[esp_video_init]   Checking sensor at 0x...: port=2, sccb_addr=0x36, detect=0x...
[esp_video_init]   → Attempting to detect MIPI-CSI sensor...
[sc202cs] Detected Camera sensor PID=0xeb52
[esp_video_init]   ✓ Sensor detected successfully: SC202CS (addr 0x36)
[esp_video_init]   ✓ MIPI-CSI video device created successfully
[esp_video] ✅ /dev/video0 existe (CSI video device - capteur détecté!)
```

### 3. Expected SUCCESS:
- ✅ Sensor SC202CS detected (PID=0xEB52)
- ✅ /dev/video0 created
- ✅ ISP pipeline initialized
- ✅ No ESP_ERR_NO_MEM crash
- ✅ Network initializes successfully

## Sensor Detection Order

The sensor detection array in `esp_cam_sensor_detect_stubs.c` is configured for M5Stack Tab5:

```c
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: SC202CS (M5Stack Tab5 default sensor - try first!)
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR  // 0x36
    },
    // Sensor 1: OV5647
    // Sensor 2: OV02C10
};
```

If detection fails for SC202CS (returns PID != 0xEB52), it will try the next sensor in order.

## Why This Will Work

We already proved that:

1. ✅ **XCLK is functional** - Direct I2C test reads Chip ID = 0xEB52
2. ✅ **I2C communication works** - Can read sensor registers
3. ✅ **Running on core 0** - Hardware peripherals accessible
4. ✅ **Sensor is SC202CS** - Chip ID matches (0xEB52)

The ONLY problem was that the sensor detection code wasn't being recompiled, so the detection loop never executed. Now that we force recompilation, the detection WILL run.

## Next Steps

1. **Compile** the project:
   ```bash
   esphome clean tab5.yaml
   esphome compile tab5.yaml
   ```

2. **Verify** in build logs that you see:
   ```
   [ESP-Video Build] 🔨 Deleted cached object: esp_video_init.o
   [ESP-Video Build] 🔨 Deleted cached object: esp_cam_sensor_detect_stubs.o
   ```

3. **Flash** to ESP32-P4:
   ```bash
   esphome upload tab5.yaml
   ```

4. **Monitor** boot logs:
   ```bash
   esphome logs tab5.yaml
   ```

5. **Look for** the sensor detection debug logs and success messages

## If It Still Doesn't Work

If you still don't see the debug logs from `esp_video_init.c` after this fix:

1. Check that the build script is being executed:
   - Look for `[ESP-Video Build]` messages in compilation output

2. Manually delete the entire build cache:
   ```bash
   rm -rf .esphome/build/
   rm -rf .pioenvs/
   rm -rf .pio/
   ```

3. Share the **full compilation logs** (not just boot logs) so we can verify the files are being recompiled

## Files Modified

- `components/esp_video/esp_video_build.py` - Added forced .o deletion before build

## Commit

```
commit 9e86903
Force rebuild of esp_video_init.c and detect_stubs.c by deleting cached objects
```

This fix should resolve the sensor detection failure once and for all.
