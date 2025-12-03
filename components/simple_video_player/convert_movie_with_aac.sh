#!/bin/bash

# Convert video to ESP32-P4 compatible format with AAC audio
# Usage: ./convert_movie_with_aac.sh input.mp4 output.mp4 640:480

if [ $# -lt 2 ]; then
    echo "Usage: $0 input_file output_file [frame_size]"
    echo "Example: $0 input.mp4 output_esp32.mp4 640:480"
    exit 1
fi

input_file="$1"
output_file="$2"
frame_size="${3:-640:480}"  # Default 640x480

if [ ! -f "$input_file" ]; then
    echo "Error: Input file not found: $input_file"
    exit 1
fi

echo "========================================"
echo "ESP32-P4 Video Converter (with AAC audio)"
echo "========================================"
echo "Input: $input_file"
echo "Output: $output_file"
echo "Frame size: $frame_size"
echo ""

# Convert with AAC audio
ffmpeg -i "$input_file" \
  -vf "scale=$frame_size:force_original_aspect_ratio=increase,crop=$frame_size,format=yuv420p" \
  -r 15 \
  -c:v libx264 \
  -profile:v baseline \
  -level:v 3.1 \
  -preset veryslow \
  -tune fastdecode \
  -crf 23 \
  -maxrate 500k \
  -bufsize 1000k \
  -g 15 \
  -bf 0 \
  -pix_fmt yuv420p \
  -colorspace:v bt709 \
  -color_primaries:v bt709 \
  -color_trc:v bt709 \
  -color_range:v tv \
  -x264opts slices=1 \
  -movflags +faststart \
  -c:a aac \
  -b:a 64k \
  -ar 44100 \
  -ac 2 \
  -y "$output_file"

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ Conversion successful!"
    echo ""
    echo "Output file: $output_file"
    ls -lh "$output_file"
    echo ""
    echo "Verification:"
    ffprobe -v error -show_entries stream=codec_name,codec_type,profile,width,height,sample_rate,channels -of default=noprint_wrappers=1 "$output_file"
    echo ""
    echo "Expected audio format:"
    echo "  codec_name=aac"
    echo "  sample_rate=44100"
    echo "  channels=2"
else
    echo ""
    echo "✗ Conversion failed!"
    exit 1
fi
