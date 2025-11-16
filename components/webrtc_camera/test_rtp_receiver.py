#!/usr/bin/env python3
"""
Test RTP H.264 receiver for ESP32-P4 WebRTC Camera

This script receives RTP packets from the ESP32 and saves them to a file
that can be played with VLC or FFmpeg.

Usage:
    python test_rtp_receiver.py --port 5004 --output stream.h264

    # Then play with:
    ffplay stream.h264
    vlc stream.h264
"""

import socket
import struct
import argparse
import sys
from datetime import datetime


class RTPPacket:
    """RTP packet parser"""

    def __init__(self, data):
        self.data = data
        self.valid = False

        if len(data) < 12:
            return

        # Parse RTP header
        byte0 = data[0]
        byte1 = data[1]

        self.version = (byte0 >> 6) & 0x03
        self.padding = (byte0 >> 5) & 0x01
        self.extension = (byte0 >> 4) & 0x01
        self.csrc_count = byte0 & 0x0F

        self.marker = (byte1 >> 7) & 0x01
        self.payload_type = byte1 & 0x7F

        self.sequence = struct.unpack('>H', data[2:4])[0]
        self.timestamp = struct.unpack('>I', data[4:8])[0]
        self.ssrc = struct.unpack('>I', data[8:12])[0]

        # Calculate header size
        header_size = 12 + (self.csrc_count * 4)

        if len(data) > header_size:
            self.payload = data[header_size:]
            self.valid = True
        else:
            self.payload = b''


class H264Receiver:
    """Receive and save H.264 RTP stream"""

    def __init__(self, port=5004, output_file='stream.h264'):
        self.port = port
        self.output_file = output_file
        self.socket = None
        self.file = None
        self.packet_count = 0
        self.byte_count = 0
        self.last_seq = None
        self.dropped_packets = 0

    def start(self):
        """Start receiving RTP packets"""
        print(f"[INFO] Starting RTP receiver on port {self.port}")
        print(f"[INFO] Output file: {self.output_file}")

        # Create UDP socket
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(('0.0.0.0', self.port))

        # Open output file
        self.file = open(self.output_file, 'wb')

        print(f"[INFO] Waiting for RTP packets...")
        print(f"[INFO] Press Ctrl+C to stop\n")

        try:
            while True:
                data, addr = self.socket.recvfrom(2048)
                self.process_packet(data, addr)

        except KeyboardInterrupt:
            print("\n[INFO] Stopping receiver...")
            self.stop()

    def process_packet(self, data, addr):
        """Process received RTP packet"""
        packet = RTPPacket(data)

        if not packet.valid:
            print(f"[WARN] Invalid RTP packet from {addr}")
            return

        # Check for dropped packets
        if self.last_seq is not None:
            expected_seq = (self.last_seq + 1) & 0xFFFF
            if packet.sequence != expected_seq:
                dropped = (packet.sequence - expected_seq) & 0xFFFF
                self.dropped_packets += dropped
                print(f"[WARN] Dropped {dropped} packets (seq {expected_seq} -> {packet.sequence})")

        self.last_seq = packet.sequence

        # Extract H.264 payload
        if len(packet.payload) > 0:
            # Check NAL unit type
            nal_header = packet.payload[0]
            nal_type = nal_header & 0x1F

            nal_type_names = {
                1: "SLICE",
                5: "IDR_SLICE",
                6: "SEI",
                7: "SPS",
                8: "PPS",
                9: "AUD"
            }

            nal_name = nal_type_names.get(nal_type, f"TYPE_{nal_type}")

            # Write NAL unit with start code
            start_code = b'\x00\x00\x00\x01'
            self.file.write(start_code)
            self.file.write(packet.payload)
            self.file.flush()

            self.packet_count += 1
            self.byte_count += len(packet.payload)

            # Print status every 30 packets
            if self.packet_count % 30 == 0:
                print(f"[RTP] Seq: {packet.sequence:5d} | "
                      f"TS: {packet.timestamp:10d} | "
                      f"Size: {len(packet.payload):5d} bytes | "
                      f"NAL: {nal_name:10s} | "
                      f"Total: {self.byte_count:8d} bytes | "
                      f"Dropped: {self.dropped_packets:3d}")

    def stop(self):
        """Stop receiver and close resources"""
        if self.file:
            self.file.close()
            print(f"\n[INFO] Saved {self.byte_count} bytes to {self.output_file}")

        if self.socket:
            self.socket.close()

        print(f"[INFO] Received {self.packet_count} packets")
        print(f"[INFO] Dropped {self.dropped_packets} packets")

        if self.packet_count > 0:
            print(f"\n[INFO] You can now play the stream with:")
            print(f"       ffplay {self.output_file}")
            print(f"       vlc {self.output_file}")


def main():
    parser = argparse.ArgumentParser(
        description='RTP H.264 receiver for ESP32-P4 WebRTC Camera'
    )
    parser.add_argument(
        '-p', '--port',
        type=int,
        default=5004,
        help='RTP port to listen on (default: 5004)'
    )
    parser.add_argument(
        '-o', '--output',
        type=str,
        default=f'stream_{datetime.now().strftime("%Y%m%d_%H%M%S")}.h264',
        help='Output H.264 file (default: stream_YYYYMMDD_HHMMSS.h264)'
    )

    args = parser.parse_args()

    receiver = H264Receiver(port=args.port, output_file=args.output)
    receiver.start()


if __name__ == '__main__':
    main()
