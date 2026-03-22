import os
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_DATA,
    CONF_PATH,
)
from esphome.core import CORE

CODEOWNERS = ["@youkorr"]

CONF_USB_MEDIA_STORAGE_ID = "usb_media_storage_id"
CONF_MOUNT_POINT = "mount_point"

usb_media_storage_ns = cg.esphome_ns.namespace("usb_media_storage")
UsbMediaStorage = usb_media_storage_ns.class_("UsbMediaStorage", cg.Component)

# Actions
UsbWriteFileAction = usb_media_storage_ns.class_("UsbWriteFileAction", automation.Action)
UsbAppendFileAction = usb_media_storage_ns.class_("UsbAppendFileAction", automation.Action)
UsbCreateDirectoryAction = usb_media_storage_ns.class_("UsbCreateDirectoryAction", automation.Action)
UsbRemoveDirectoryAction = usb_media_storage_ns.class_("UsbRemoveDirectoryAction", automation.Action)
UsbDeleteFileAction = usb_media_storage_ns.class_("UsbDeleteFileAction", automation.Action)

def validate_raw_data(value):
    if isinstance(value, str):
        return value.encode("utf-8")
    if isinstance(value, list):
        return cv.Schema([cv.hex_uint8_t])(value)
    raise cv.Invalid(
        "data must either be a string wrapped in quotes or a list of bytes"
    )

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UsbMediaStorage),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Add usb_host_msc library via extra build script
    component_dir = os.path.dirname(__file__)
    msc_dir = os.path.join(component_dir, "components", "usb_host_msc")
    cg.add_build_flag(f"-I{os.path.join(msc_dir, 'include')}")
    cg.add_build_flag(f"-I{os.path.join(msc_dir, 'private_include')}")

    # Pass msc_dir to the build script via environment variable
    os.environ["USB_HOST_MSC_DIR"] = msc_dir

    # Use extra_scripts to compile usb_host_msc sources directly
    build_script = os.path.join(component_dir, "build_msc.py")
    cg.add_platformio_option("extra_scripts", [f"pre:{build_script}"])

    # Framework-specific build flags
    cg.add_build_flag("-DUSE_USB_MEDIA_STORAGE")


USB_PATH_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(UsbMediaStorage),
        cv.Required(CONF_PATH): cv.templatable(cv.string_strict),
    }
)

USB_WRITE_FILE_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(UsbMediaStorage),
        cv.Required(CONF_PATH): cv.templatable(cv.string_strict),
        cv.Required(CONF_DATA): cv.templatable(validate_raw_data),
    }
).extend(USB_PATH_ACTION_SCHEMA)

@automation.register_action(
    "usb_media_storage.write_file", UsbWriteFileAction, USB_WRITE_FILE_ACTION_SCHEMA, synchronous=True
)
async def usb_write_file_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    path_ = await cg.templatable(config[CONF_PATH], args, cg.std_string)
    data_ = await cg.templatable(config[CONF_DATA], args, cg.std_vector.template(cg.uint8))
    cg.add(var.set_path(path_))
    cg.add(var.set_data(data_))
    return var


@automation.register_action(
    "usb_media_storage.append_file", UsbAppendFileAction, USB_WRITE_FILE_ACTION_SCHEMA, synchronous=True
)
async def usb_append_file_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    path_ = await cg.templatable(config[CONF_PATH], args, cg.std_string)
    data_ = await cg.templatable(config[CONF_DATA], args, cg.std_vector.template(cg.uint8))
    cg.add(var.set_path(path_))
    cg.add(var.set_data(data_))
    return var


@automation.register_action(
    "usb_media_storage.create_directory", UsbCreateDirectoryAction, USB_PATH_ACTION_SCHEMA, synchronous=True
)
async def usb_create_directory_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    path_ = await cg.templatable(config[CONF_PATH], args, cg.std_string)
    cg.add(var.set_path(path_))
    return var


@automation.register_action(
    "usb_media_storage.remove_directory", UsbRemoveDirectoryAction, USB_PATH_ACTION_SCHEMA, synchronous=True
)
async def usb_remove_directory_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    path_ = await cg.templatable(config[CONF_PATH], args, cg.std_string)
    cg.add(var.set_path(path_))
    return var


@automation.register_action(
    "usb_media_storage.delete_file", UsbDeleteFileAction, USB_PATH_ACTION_SCHEMA, synchronous=True
)
async def usb_delete_file_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    path_ = await cg.templatable(config[CONF_PATH], args, cg.std_string)
    cg.add(var.set_path(path_))
    return var
