"""
Minimal button component stub for header compatibility.

This component exists ONLY to provide the button.h header file
required by ESPHome core (application.h, controller.h).

It does NOT implement any button functionality - that comes from
ESPHome's built-in button component.

CRITICAL: This component should NOT be loaded via AUTO_LOAD or
external_components. It exists purely for C++ header includes.
"""
import esphome.codegen as cg

# Define the button namespace for C++ code generation
button_ns = cg.esphome_ns.namespace("button")
Button = button_ns.class_("Button", cg.EntityBase)

# Do NOT define ESPHOME_ENTITY_BUTTON_COUNT here!
# Let ESPHome core handle button entity counting.
