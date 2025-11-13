# ESP-DL Local Installation

ESP-DL (Espressif Deep Learning Library) v3.1.0 is included directly in this repository at `components/esp-dl/`.

## Why Local Instead of Package Manager?

1. **ESPHome Compatibility**: ESPHome doesn't support IDF Component Manager dependencies properly
2. **No Build Conflicts**: Avoids version conflicts with managed_components
3. **Faster Builds**: No network downloads during compilation
4. **Guaranteed Version**: Exact ESP-DL v3.1.0 is embedded

## Directory Structure

```
components/
├── esp-dl/                          ← ESP-DL library (local)
│   ├── CMakeLists.txt               ← ESP-IDF build configuration
│   ├── dl/                          ← Deep learning operations
│   │   ├── base/                    ← Base operations (conv2d, pool, etc.)
│   │   ├── model/                   ← Model loading and management
│   │   └── module/                  ← High-level modules
│   ├── vision/                      ← Computer vision modules
│   │   ├── detect/                  ← Object/face detection
│   │   ├── image/                   ← Image preprocessing
│   │   ├── classification/          ← Image classification
│   │   └── recognition/             ← Face recognition
│   ├── fbs_loader/                  ← FlatBuffers model loader
│   └── audio/                       ← Audio processing (optional)
│
├── human_face_detect/               ← Our face detection component
│   ├── human_face_detect_espdl.h    ← ESP-DL wrappers (MSR+MNP)
│   └── human_face_detect_espdl.cpp  ← Implementation
```

## How It Works

### 1. ESP-IDF Build System

ESP-IDF automatically discovers components in `components/` directory:
- Finds `components/esp-dl/CMakeLists.txt`
- Compiles ESP-DL as a library
- Links it with human_face_detect

### 2. Include Paths

In `human_face_detect/__init__.py`, we add explicit include paths:

```python
esp_dl_dir = os.path.join(component_dir, "..", "esp-dl")

cg.add_build_flag(f"-I{esp_dl_dir}")
cg.add_build_flag(f"-I{esp_dl_dir}/dl/base")
cg.add_build_flag(f"-I{esp_dl_dir}/dl/model/include")
cg.add_build_flag(f"-I{esp_dl_dir}/vision/detect")
cg.add_build_flag(f"-I{esp_dl_dir}/vision/image")
cg.add_build_flag(f"-I{esp_dl_dir}/fbs_loader/include")
```

### 3. Headers Available

```cpp
// In human_face_detect_espdl.h
#include "dl_detect_base.hpp"              // ✅ Found in vision/detect/
#include "dl_detect_msr_postprocessor.hpp" // ✅ Found in vision/detect/
#include "dl_detect_mnp_postprocessor.hpp" // ✅ Found in vision/detect/
```

## Advantages Over Package Manager

| Aspect | Local (This Repo) | Package Manager |
|--------|-------------------|-----------------|
| **Compatibility** | ✅ Works with ESPHome | ❌ Conflicts with PlatformIO |
| **Build Speed** | ✅ Fast (no download) | ⚠️ Slow (downloads each time) |
| **Version Control** | ✅ Git tracks exact version | ❌ May update unexpectedly |
| **Offline Builds** | ✅ Works offline | ❌ Requires internet |
| **Debugging** | ✅ Can modify source | ❌ Read-only managed_components |

## File Size

- **Total**: ~110K lines of code
- **Compiled**: ~4.4 MB (libfbs_model.a)
- **Models**: ~187 KB (MSR + MNP)

## Source

ESP-DL v3.1.0 from official Espressif repository:
https://github.com/espressif/esp-dl/tree/v3.1.0

## Usage in human_face_detect

```cpp
// Automatically includes ESP-DL when ESPHOME_HAS_ESP_DL is defined
#if defined(USE_ESP_IDF) && __has_include("dl_detect_base.hpp")
#define ESPHOME_HAS_ESP_DL 1

#include "dl_detect_base.hpp"
#include "dl_detect_mnp_postprocessor.hpp"
#include "dl_detect_msr_postprocessor.hpp"

namespace esphome {
namespace human_face_detect {

class MSRMNPDetector : public dl::detect::Detect {
  // Face detection implementation using ESP-DL
};

}  // namespace human_face_detect
}  // namespace esphome

#endif  // ESPHOME_HAS_ESP_DL
```

## No Configuration Needed

ESP-IDF component system handles everything automatically:
1. Discovers `components/esp-dl/` at build time
2. Compiles ESP-DL library
3. Links with human_face_detect
4. Includes are resolved via `-I` flags

✅ Just works!
