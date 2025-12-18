#!/usr/bin/env python3
"""Create embedded C files from .espdl models"""

import os

def create_embed_c(espdl_file, c_file, symbol_name):
    """Create embedded .c file from .espdl binary"""
    with open(espdl_file, 'rb') as f:
        model_data = f.read()

    c_content = f'''// Auto-generated - embedded {os.path.basename(espdl_file)} model
#include <stddef.h>
#include <stdint.h>

__attribute__((aligned(16)))
const uint8_t {symbol_name}[] = {{
'''
    for i in range(0, len(model_data), 16):
        chunk = model_data[i:i+16]
        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
        c_content += f'    {hex_bytes},\n'

    c_content += f'''}};

const uint8_t *{symbol_name}_end = {symbol_name} + {len(model_data)};
const size_t {symbol_name}_size = {len(model_data)};
'''

    with open(c_file, 'w') as f:
        f.write(c_content)

    print(f"Created {c_file}: {len(model_data)} bytes")

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # Create human_face_detect embed
    create_embed_c(
        os.path.join(script_dir, "human_face_detect.espdl"),
        os.path.join(script_dir, "human_face_detect_espdl_embed.c"),
        "_binary_human_face_detect_espdl_start"
    )

    # Create human_face_feat embed
    create_embed_c(
        os.path.join(script_dir, "human_face_feat.espdl"),
        os.path.join(script_dir, "human_face_feat_espdl_embed.c"),
        "_binary_human_face_feat_mfn_s8_v1_espdl_start"
    )
