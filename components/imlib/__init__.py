import esphome.codegen as cg

# imlib component for image processing (drawing, font, math)
# This is a pure C library compiled as an IDF component
# No ESPHome-specific configuration needed

CODEOWNERS = ["@youkorr"]

def to_code(config):
    # imlib is compiled via CMakeLists.txt, no additional code generation needed
    pass
