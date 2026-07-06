"""Somfy io-homecontrol pairing button.

Triggers Add/Pair/Remove against one specific cover's bonded virtual remote
identity (IOHC::IOHCRemote1W) - see iohc_remote1w.h for what each of these
actually transmits. This is deliberately a separate button entity rather than
an automatic action: Add/Remove write bonding state to the real motor and
should only ever fire when the user means it to.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID, CONF_TYPE

from .. import iohc_ns
from ..cover import IOHCCover

CODEOWNERS = ["@danielpetrovic"]
DEPENDENCIES = ["iohc"]

CONF_COVER_ID = "cover_id"

IOHCPairButton = iohc_ns.class_("IOHCPairButton", button.Button, cg.Component)

# IOHC::RemoteButton lives in the global "IOHC" namespace (iohc_remote1w.h),
# not esphome::iohc - a separate C++ namespace from this component's own
# esphome::iohc, so it needs its own MockObj reference here.
remote_button_ns = cg.global_ns.namespace("IOHC")
RemoteButton = remote_button_ns.enum("RemoteButton", is_class=True)

TYPES = {
    "add": RemoteButton.Add,
    "pair": RemoteButton.Pair,
    "remove": RemoteButton.Remove,
}

CONFIG_SCHEMA = button.button_schema(IOHCPairButton).extend(
    {
        cv.Required(CONF_COVER_ID): cv.use_id(IOHCCover),
        cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)

    cover_var = await cg.get_variable(config[CONF_COVER_ID])
    cg.add(var.set_cover(cover_var))
    cg.add(var.set_button_type(config[CONF_TYPE]))
