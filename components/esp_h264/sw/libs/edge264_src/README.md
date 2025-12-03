# edge264 H.264 Decoder Integration

This directory should contain the edge264 source files for multi-profile H.264 decoding support.

## Why edge264?

- **Lightweight**: Simple C implementation with minimal dependencies
- **Multi-profile**: Supports Progressive High and MVC 3D profiles
- **Performance**: Uses 128-bit vector optimizations
- **License**: BSD-3-Clause (commercial-friendly)

## How to Download edge264

1. Visit the edge264 GitHub repository:
   https://github.com/tvlabs/edge264

2. Download version v0.6.0 (or latest stable):
   ```bash
   cd /tmp
   wget https://github.com/tvlabs/edge264/archive/refs/tags/v0.6.0.tar.gz
   tar -xzf v0.6.0.tar.gz
   ```

3. Copy the source files to this directory:
   ```bash
   cp edge264-0.6.0/*.c edge264-0.6.0/*.h /path/to/this/directory/
   ```

## Required Files

Place these files in this directory:
- `edge264.c`
- `edge264.h`
- `edge264_bitstream.c`
- `edge264_deblock.c`
- `edge264_headers.c`
- `edge264_inter.c`
- `edge264_intra.c`
- `edge264_internal.h`
- `edge264_mvpred.c`
- `edge264_residual.c`
- `edge264_sei.c`
- `edge264_slice.c`

## Alternative: Automated Download

Or use this one-liner to download automatically:
```bash
cd components/esp_h264/sw/libs/edge264_src
wget -qO- https://github.com/tvlabs/edge264/archive/refs/tags/v0.6.0.tar.gz | tar xz --strip-components=1 '*.c' '*.h'
```

## Build Integration

Once the files are in place, the build system will automatically:
1. Compile edge264 sources
2. Link with the esp_h264_dec_sw wrapper
3. Enable multi-profile H.264 decoding (Baseline, Main, High)

## Status

- [ ] edge264 sources downloaded
- [ ] Files placed in this directory
- [ ] Build system configured
- [ ] Decoder wrapper created
- [ ] Firmware compiled successfully
- [ ] Multi-profile decoding tested

## License

edge264 is licensed under BSD-3-Clause license.
Copyright (c) 2020 TVLabs
See: https://github.com/tvlabs/edge264/blob/master/LICENSE
