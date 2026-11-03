import esphome.codegen as cg
from esphome.components import melcloud, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE
from esphome.types import ConfigType

DEPENDENCIES = ["wifi", "melcloud"]


def AUTO_LOAD() -> list[str]:
    auto_load = ["web_server_base"]
    if CORE.is_esp32:
        auto_load.append("socket")
    return auto_load

setup_portal_ns = cg.esphome_ns.namespace("setup_portal")
SetupPortal = setup_portal_ns.class_("SetupPortal", cg.Component)

CONF_MELCLOUD_ID = "melcloud_id"


def _final_validate(config: ConfigType) -> None:
    from esphome.components import socket

    socket.consume_sockets(3, "setup_portal")(config)
    socket.consume_sockets(1, "setup_portal", socket.SocketType.UDP)(config)


FINAL_VALIDATE_SCHEMA = _final_validate

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SetupPortal),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.GenerateID(CONF_MELCLOUD_ID): cv.use_id(melcloud.MelCloud),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config: ConfigType) -> None:
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    account = await cg.get_variable(config[CONF_MELCLOUD_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)
    cg.add(var.set_melcloud(account))
