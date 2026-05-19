import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PATH
from esphome.core import CORE
import os
import json

MULTI_CONF = True

file_ns = cg.esphome_ns.namespace("file_component")
FileData = file_ns.class_("FileData", cg.Component)

CONFIG_SCHEMA = cv.ensure_list(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(FileData),
            cv.Required(CONF_PATH): cv.string,
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    component_dir = os.path.dirname(os.path.abspath(__file__))
    embed_info = []

    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)

        path = conf[CONF_PATH]
        if not os.path.isabs(path):
            path = CORE.relative_config_path(path)

        safe_id = str(conf[CONF_ID]).replace("-", "_").replace(" ", "_")

        # Generate C embed file
        with open(path, "rb") as f:
            data = f.read()

        c_path = os.path.join(component_dir, f"file_embed_{safe_id}.c")
        with open(c_path, "w") as f:
            f.write("#include <stdint.h>\n")
            f.write("#include <stddef.h>\n\n")
            f.write("__attribute__((aligned(16)))\n")
            f.write(f"const uint8_t file_{safe_id}_data[] = {{\n")
            for i in range(0, len(data), 16):
                chunk = data[i : i + 16]
                f.write(
                    "    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n"
                )
            f.write("};\n\n")
            f.write(f"const size_t file_{safe_id}_size = {len(data)};\n")

        embed_info.append({"id": safe_id, "c_file": c_path})

        # Add extern declarations and set data
        cg.add_global(
            cg.RawExpression(
                f'extern "C" {{ extern const uint8_t file_{safe_id}_data[]; '
                f"extern const size_t file_{safe_id}_size; }}"
            )
        )
        cg.add(
            var.set_data(
                cg.RawExpression(f"file_{safe_id}_data"),
                cg.RawExpression(f"(size_t)file_{safe_id}_size"),
            )
        )

    # Write embed info for build script
    info_path = os.path.join(component_dir, ".file_embed_info.json")
    with open(info_path, "w") as f:
        json.dump(embed_info, f)

    # Register build script
    build_script = os.path.join(component_dir, "file_build.py")
    if os.path.exists(build_script):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script}"])
