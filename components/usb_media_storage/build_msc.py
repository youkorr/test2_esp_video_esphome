import os

Import("env")

msc_dir = os.environ.get("USB_HOST_MSC_DIR", "")

if msc_dir and os.path.isdir(os.path.join(msc_dir, "src")):
    env.BuildSources(
        os.path.join("$BUILD_DIR", "usb_host_msc"),
        os.path.join(msc_dir, "src"),
    )
    env.Append(CPPPATH=[
        os.path.join(msc_dir, "include"),
        os.path.join(msc_dir, "private_include"),
    ])
