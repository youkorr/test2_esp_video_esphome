# AAC Audio Integration Guide for Simple Video Player

## 📋 Context

The `simple_video_player` component currently has **audio support disabled** (`USE_ESP_AUDIO_CODEC = 0`) because the previous implementation was not working correctly. This guide explains how to re-enable AAC audio using the **esp_audio_codec** library (already available in `/components/esp_audio_codec`).

## 🎯 Goal

Enable AAC audio decoding in MP4 files with proper audio/video synchronization.

---

## 📦 Available Components

### 1. ESP Audio Codec Library

**Location**: `/components/esp_audio_codec`

**Already available**:
- ✅ `esp_aac_dec.h` - AAC decoder implementation
- ✅ `esp_audio_dec.h` - Generic audio decoder API
- ✅ `esp_m4a_dec.h` - M4A container parser (MP4 audio)
- ✅ `esp_audio_types.h` - Audio data structures

**API Functions**:
```cpp
#include "decoder/impl/esp_aac_dec.h"

// Open AAC decoder
esp_audio_dec_handle_t aac_dec = esp_aac_dec_open(NULL);

// Decode frame
esp_audio_dec_in_raw_t in_raw = {
    .buffer = audio_data,       // AAC compressed data
    .len = audio_size,          // Size in bytes
    .pts = timestamp_ms         // Presentation timestamp
};

esp_audio_dec_out_raw_t out_raw = {
    .buffer = pcm_buffer,       // PCM output buffer
    .len = pcm_buffer_size      // Max output size
};

int ret = esp_audio_dec_decode(aac_dec, &in_raw, &out_raw);

// Get decoded sample info
esp_audio_dec_info_t info;
esp_audio_dec_get_info(aac_dec, &info);
// info.sample_rate (e.g. 44100 Hz)
// info.bits_per_sample (e.g. 16 bits)
// info.channel (e.g. 2 = stereo)

// Close decoder
esp_audio_dec_close(aac_dec);
```

### 2. ESPHome Speaker Component

**Already available**: `speaker::Speaker *speaker_`

**API**:
```cpp
// Play PCM samples
speaker_->play(pcm_data, pcm_size);

// Check if speaker is ready
if (speaker_->is_ready()) {
    // OK to play
}
```

---

## 🔧 Implementation Steps

### Step 1: Enable Audio Codec in Header

**File**: `components/simple_video_player/simple_video_player.h`

```cpp
// Change line 39:
#define USE_ESP_AUDIO_CODEC 0  // ❌ Currently disabled

// To:
#define USE_ESP_AUDIO_CODEC 1  // ✅ Enable
```

**Add includes**:
```cpp
#if USE_ESP_AUDIO_CODEC
#include "decoder/impl/esp_aac_dec.h"
#include "decoder/esp_audio_dec.h"
#include "decoder/esp_audio_types.h"
#endif
```

**Uncomment variables** (lines 414-421):
```cpp
// Audio codec variables (currently commented out)
esp_audio_dec_handle_t aac_decoder_{nullptr};  // ✅ Uncomment
uint8_t *audio_input_buffer_{nullptr};         // ✅ Uncomment
uint8_t *audio_output_buffer_{nullptr};        // ✅ Uncomment (PCM output)
size_t audio_input_size_{0};                   // ✅ Uncomment
size_t audio_output_size_{0};                  // ✅ Uncomment (PCM size)
bool has_audio_{false};                        // ✅ Uncomment
bool aac_decoder_ready_{false};                // ✅ Uncomment
```

**Uncomment function declarations** (lines 245-248):
```cpp
bool init_aac_decoder_();       // ✅ Uncomment
bool read_next_audio_sample_(); // ✅ Uncomment
bool decode_audio_frame_();     // ✅ Uncomment
void process_audio_();          // ✅ Uncomment
```

---

### Step 2: Implement AAC Decoder Initialization

**File**: `components/simple_video_player/simple_video_player.cpp`

**Location**: Around line 2529 (currently commented out)

```cpp
bool SimpleVideoPlayer::init_aac_decoder_() {
  if (!this->has_audio_ || this->speaker_ == nullptr) {
    return false;
  }

  // Open AAC decoder
  this->aac_decoder_ = esp_aac_dec_open(NULL);
  if (!this->aac_decoder_) {
    ESP_LOGE(TAG, "Failed to open AAC decoder");
    return false;
  }

  // Send AAC config (from esds box)
  if (!this->audio_config_.empty()) {
    esp_audio_dec_in_raw_t config_raw = {
        .buffer = this->audio_config_.data(),
        .len = this->audio_config_.size(),
        .pts = 0
    };

    esp_audio_dec_out_raw_t dummy_out = {
        .buffer = nullptr,
        .len = 0
    };

    int ret = esp_audio_dec_decode(this->aac_decoder_, &config_raw, &dummy_out);
    if (ret != 0) {
      ESP_LOGW(TAG, "Failed to send AAC config (may work anyway)");
    }
  }

  // Get decoder info
  esp_audio_dec_info_t info;
  if (esp_audio_dec_get_info(this->aac_decoder_, &info) == 0) {
    this->audio_sample_rate_ = info.sample_rate;
    this->audio_channels_ = info.channel;
    ESP_LOGI(TAG, "AAC decoder initialized: %u Hz, %u channels, %u bits",
             info.sample_rate, info.channel, info.bits_per_sample);
  }

  // Allocate buffers
  this->audio_input_size_ = 8192;  // Max AAC frame size
  this->audio_output_size_ = 8192 * 4;  // PCM output (larger)

  this->audio_input_buffer_ = (uint8_t*)heap_caps_malloc(
      this->audio_input_size_, MALLOC_CAP_SPIRAM);
  this->audio_output_buffer_ = (uint8_t*)heap_caps_malloc(
      this->audio_output_size_, MALLOC_CAP_SPIRAM);

  if (!this->audio_input_buffer_ || !this->audio_output_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate audio buffers");
    return false;
  }

  this->aac_decoder_ready_ = true;
  ESP_LOGI(TAG, "AAC decoder ready");
  return true;
}
```

---

### Step 3: Parse Audio Samples from MP4

**Location**: Lines 2502-2503 (uncomment)

```cpp
if (tag == 0x05) {  // DecoderSpecificInfo
  // AAC config
  this->audio_config_.resize(len);
  this->cached_fread_(this->audio_config_.data(), 1, len);
  this->has_audio_ = true;  // ✅ Uncomment this line
  ESP_LOGI(TAG, "Found AAC config: %d bytes", len);
  break;
}
```

**Audio samples are already parsed** in:
- `parse_stts_()` - Sample durations (audio track)
- `parse_stsc_()` - Sample-to-chunk mapping
- `parse_stsz_()` - Sample sizes
- `parse_stco_()` - Chunk offsets

They're stored in `std::vector<AudioSample> audio_samples_` (already defined).

---

### Step 4: Implement Audio Decoding and Playback

**File**: `components/simple_video_player/simple_video_player.cpp`

```cpp
bool SimpleVideoPlayer::decode_audio_frame_() {
  if (!this->aac_decoder_ready_ || this->current_audio_sample_ >= this->audio_samples_.size()) {
    return false;
  }

  // Get current audio sample
  AudioSample &sample = this->audio_samples_[this->current_audio_sample_];

  // Seek to sample position
  this->cached_fseek_(sample.offset, SEEK_SET);

  // Read compressed AAC data
  size_t bytes_to_read = std::min(sample.size, (uint32_t)this->audio_input_size_);
  size_t bytes_read = this->cached_fread_(this->audio_input_buffer_, 1, bytes_to_read);

  if (bytes_read != bytes_to_read) {
    ESP_LOGE(TAG, "Failed to read audio sample");
    return false;
  }

  // Decode AAC → PCM
  esp_audio_dec_in_raw_t in_raw = {
      .buffer = this->audio_input_buffer_,
      .len = bytes_read,
      .pts = sample.timestamp_ms
  };

  esp_audio_dec_out_raw_t out_raw = {
      .buffer = this->audio_output_buffer_,
      .len = this->audio_output_size_
  };

  int ret = esp_audio_dec_decode(this->aac_decoder_, &in_raw, &out_raw);
  if (ret != 0) {
    ESP_LOGW(TAG, "AAC decode failed: %d", ret);
    return false;
  }

  // Play PCM audio via speaker
  if (this->speaker_ && this->speaker_->is_ready() && out_raw.len > 0) {
    this->speaker_->play(this->audio_output_buffer_, out_raw.len);
  }

  this->current_audio_sample_++;
  return true;
}
```

---

### Step 5: Audio/Video Synchronization

**Modify playback loop** to check timestamps:

```cpp
void SimpleVideoPlayer::decode_task_() {
  while (1) {
    // ... existing video decode code ...

    // Process audio if available
    if (this->has_audio_ && this->aac_decoder_ready_) {
      // Get video timestamp
      uint32_t video_ts = this->current_time_ms_;

      // Decode audio samples until we catch up with video
      while (this->current_audio_sample_ < this->audio_samples_.size()) {
        AudioSample &audio_sample = this->audio_samples_[this->current_audio_sample_];

        // If audio is ahead of video, stop
        if (audio_sample.timestamp_ms > video_ts + 100) {
          break;
        }

        // Decode and play audio
        this->decode_audio_frame_();
      }
    }
  }
}
```

---

### Step 6: Cleanup Audio Resources

**In `stop()` function**, add:

```cpp
void SimpleVideoPlayer::stop() {
  // ... existing cleanup ...

#if USE_ESP_AUDIO_CODEC
  // Close AAC decoder
  if (this->aac_decoder_ != nullptr) {
    esp_audio_dec_close(this->aac_decoder_);
    this->aac_decoder_ = nullptr;
    this->aac_decoder_ready_ = false;
  }

  // Free audio buffers
  if (this->audio_input_buffer_) {
    heap_caps_free(this->audio_input_buffer_);
    this->audio_input_buffer_ = nullptr;
  }
  if (this->audio_output_buffer_) {
    heap_caps_free(this->audio_output_buffer_);
    this->audio_output_buffer_ = nullptr;
  }
#endif
}
```

---

### Step 7: Build Configuration

**File**: `components/simple_video_player/simple_video_player_build.py`

```python
# Remove lines 209-212 (audio disabled message)

# Add esp_audio_codec to build
if USE_ESP_AUDIO_CODEC:
    import build_esp_audio_codec  # If needed
    print("[Simple Video Player] AAC audio codec enabled")
```

---

## 🧪 Testing

### Test MP4 File

Use the **Espressif test video** (known to work):
```
https://dl.espressif.com/AE/esp-dev-kits/test_video.mp4
```

**Specs**:
- Video: MJPEG 20fps RGB888 (1280x720)
- **Audio: AAC 44.1kHz stereo**

### YAML Configuration

```yaml
simple_video_player:
  - id: my_player
    file_path: "/sdcard/test_video.mp4"
    width: 800
    height: 480
    speaker: my_i2s_speaker  # ✅ Add speaker reference

speaker:
  - platform: i2s_audio
    id: my_i2s_speaker
    # ... I2S pins configuration ...
```

### Expected Logs

```
[simple_video_player] Found AAC config: 5 bytes
[simple_video_player] AAC decoder initialized: 44100 Hz, 2 channels, 16 bits
[simple_video_player] AAC decoder ready
[simple_video_player] Video: 1280x720 @ 20fps, Audio: 44100 Hz stereo
```

---

## ⚠️ Known Issues to Fix

### 1. Audio/Video Drift

**Problem**: Audio may drift out of sync over time.

**Solution**: Implement PTS-based synchronization:
```cpp
// Calculate audio clock vs video clock
int32_t drift_ms = (int32_t)audio_ts - (int32_t)video_ts;

if (drift_ms > 100) {
  // Audio ahead, skip samples
  this->current_audio_sample_++;
} else if (drift_ms < -100) {
  // Audio behind, insert silence or speed up
}
```

### 2. Buffer Underruns

**Problem**: Speaker buffer may underrun causing crackling.

**Solution**: Use larger speaker buffer in YAML:
```yaml
speaker:
  - platform: i2s_audio
    id: my_i2s_speaker
    buffer_duration: 100ms  # ✅ Increase buffer
```

### 3. Memory Usage

**Problem**: Audio buffers use ~16KB SPIRAM per decoder.

**Solution**: Already using `MALLOC_CAP_SPIRAM` (not DRAM).

---

## 📊 Performance Expectations

| Metric | Expected Value |
|--------|----------------|
| **CPU overhead** | +5-8% for AAC decode |
| **Memory** | +16KB SPIRAM (buffers) |
| **Latency** | <50ms audio/video sync |
| **Quality** | 44.1kHz 16-bit stereo |

---

## 🔗 References

- **ESP Audio Codec**: `/components/esp_audio_codec/include/decoder/impl/esp_aac_dec.h`
- **Waveshare fork audio**: Uses same `esp_codec_dev` approach
- **MP4 Parser**: Already implemented in `simple_video_player.cpp` (lines 1700-2500)
- **Test video**: https://dl.espressif.com/AE/esp-dev-kits/test_video.mp4

---

## ✅ Summary Checklist

- [ ] Enable `USE_ESP_AUDIO_CODEC = 1` in header
- [ ] Add `#include "decoder/impl/esp_aac_dec.h"`
- [ ] Uncomment audio variables and function declarations
- [ ] Implement `init_aac_decoder_()` with esp_aac_dec
- [ ] Uncomment `has_audio_ = true` in esds parser
- [ ] Implement `decode_audio_frame_()`
- [ ] Add audio processing to playback loop
- [ ] Implement audio/video synchronization
- [ ] Add cleanup in `stop()` function
- [ ] Test with Espressif test_video.mp4
- [ ] Monitor for audio/video drift
- [ ] Tune speaker buffer if needed

---

**Estimated work**: 4-6 hours for complete implementation and testing.

**Priority**: Medium (video works without audio, this is an enhancement).
