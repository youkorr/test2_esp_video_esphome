import os

Import("env")

component_dir = os.path.dirname(os.path.abspath(env.subst("$PROJECT_DIR")))
# The actual path is resolved at build time from the extra_scripts location
script_dir = os.path.dirname(os.path.abspath(__file__))
msc_dir = os.path.join(script_dir, "components", "usb_host_msc")

env.BuildSources(
    os.path.join("$BUILD_DIR", "usb_host_msc"),
    os.path.join(msc_dir, "src"),
)

env.Append(CPPPATH=[
    os.path.join(msc_dir, "include"),
    os.path.join(msc_dir, "private_include"),
])
