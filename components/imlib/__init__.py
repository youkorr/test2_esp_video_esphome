import esphome.codegen as cg
import esphome.config_validation as cv

# imlib component for image processing (drawing, font, math)
# This is a pure C library compiled as an IDF component
# No ESPHome-specific configuration needed

CODEOWNERS = ["@youkorr"]

# Empty schema - imlib is a dependency-only component
CONFIG_SCHEMA = cv.Schema({})

async def to_code(config):
    # imlib is compiled via CMakeLists.txt, no additional code generation needed
    pass
