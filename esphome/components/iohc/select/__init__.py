"""Somfy io-homecontrol per-cover mode select.

Switches one cover between 1W Timed (local travel-time position estimate),
1W My (RTS-bridge-style 3-state discrete position, no time tracking), and 2W
(real motor-reported position - not implemented yet, selecting it just logs
a warning). See IOHCCover::Mode in cover/iohc_cover.h.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from .. import iohc_ns
from ..cover import IOHCCover

CODEOWNERS = ["@danielpetrovic"]
DEPENDENCIES = ["iohc"]

CONF_COVER_ID = "cover_id"

IOHCModeSelect = iohc_ns.class_("IOHCModeSelect", select.Select, cg.Component)

MODE_OPTIONS = ["1W Timed", "1W My", "2W"]

CONFIG_SCHEMA = select.select_schema(IOHCModeSelect).extend(
    {
        cv.Required(CONF_COVER_ID): cv.use_id(IOHCCover),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await select.new_select(config, options=MODE_OPTIONS)
    await cg.register_component(var, config)

    cover_var = await cg.get_variable(config[CONF_COVER_ID])
    cg.add(var.set_cover(cover_var))
