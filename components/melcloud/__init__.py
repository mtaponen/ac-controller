import esphome.codegen as cg
from esphome.components.esp32 import (
    request_http_client,
    require_full_certificate_bundle,
)
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["json", "network"]
AUTO_LOAD = ["json"]

melcloud_ns = cg.esphome_ns.namespace("melcloud")
MelCloud = melcloud_ns.class_("MelCloud", cg.Component)

CONF_EMAIL = "email"
CONF_PASSWORD = "password"
CONF_DEVICE_ID = "device_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MelCloud),
        cv.Required(CONF_EMAIL): cv.string,
        cv.Required(CONF_PASSWORD): cv.string,
        cv.Optional(CONF_DEVICE_ID, default=""): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_email(config[CONF_EMAIL]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    require_full_certificate_bundle()
    request_http_client()
