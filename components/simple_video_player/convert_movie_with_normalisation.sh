#!/bin/bash

# Check if input and output files are provided
if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
    echo "Usage: $0 <input_file> <output_file> <frame_size (e.g. 280:240)>"
    exit 1
fi

# Input and output filenames
input_file="$1"
output_file="$2"
frame_size="$3"

# First pass: Analyze audio and extract loudness statistics
echo "Performing first pass to analyze audio..."
loudness_data=$(ffmpeg -i "$input_file" -af "loudnorm=print_format=json" -vn -sn -dn -f null -y /dev/null 2>&1 | awk '/{/,/}/' | jq -c '.')

# Extract values from JSON
input_i=$(echo "$loudness_data" | jq -r '.input_i')
input_tp=$(echo "$loudness_data" | jq -r '.input_tp')
input_lra=$(echo "$loudness_data" | jq -r '.input_lra')
input_thresh=$(echo "$loudness_data" | jq -r '.input_thresh')

# Second pass: Normalize audio and encode in H.264 Baseline (ESP32-P4 compatible)
echo "Performing second pass to normalize audio and encode..."
echo "Using H.264 Baseline profile (compatible with ESP32-P4 tinyh264 decoder)"
ffmpeg -i "$input_file" \
  -vf "scale=$frame_size:force_original_aspect_ratio=increase,crop=$frame_size" \
  -af "loudnorm=measured_i=$input_i:measured_tp=$input_tp:measured_lra=$input_lra:measured_thresh=$input_thresh:offset=0.0:linear=true:print_format=summary" \
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
  -movflags +faststart \
  -acodec pcm_u8 \
  -ar 16000 \
  -ac 1 \
  "$output_file"

echo ""
echo "✓ Conversion complete! Output saved as $output_file"
echo "  Codec: H.264 Baseline profile (ESP32-P4 compatible)"
echo "  Resolution: $frame_size @ 15 FPS"
