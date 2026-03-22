"""
Build script for file component - compiles embedded file data
"""

import os
import json
Import("env")

script_dir = Dir('.').srcnode().abspath
info_path = os.path.join(script_dir, ".file_embed_info.json")

if os.path.exists(info_path):
    with open(info_path) as f:
        embed_info = json.load(f)

    sources = []
    for entry in embed_info:
        c_file = entry["c_file"]
        if os.path.exists(c_file):
            sources.append(c_file)
            print(f"[File Embed] + {os.path.basename(c_file)}")

    if sources:
        objects = []
        for src in sources:
            try:
                obj = env.Object(src)
                objects.extend(obj)
            except Exception as e:
                print(f"[File Embed] Failed to compile {os.path.basename(src)}: {e}")

        if objects:
            lib = env.StaticLibrary(
                os.path.join("$BUILD_DIR", "libfile_embed"), objects
            )
            env.Prepend(LIBS=[lib])
            env["_LIBFLAGS"] = (
                "-Wl,--start-group " + env["_LIBFLAGS"] + " -Wl,--end-group"
            )
            print(f"[File Embed] {len(sources)} file(s) embedded")
