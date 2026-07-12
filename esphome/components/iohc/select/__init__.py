"""Somfy IO (io-homecontrol) per-cover mode select.

Switches one cover between Position (1W, default - the only mode that sends
arbitrary percentage targets to the motor; the displayed position while
moving is a cosmetic local travel-time estimate only, see IOHCCover::Mode),
Open / My / Close (1W, RTS-bridge-style 3-state discrete position, no time
tracking, no arbitrary percentages), and Two-Way (Experimental) (real 2W
commands to an actually-bonded motor - the control path is implemented, but
this bridge's own bonding (Program (2W)) has never yet completed
successfully against real hardware, so in practice every command currently
gets refused with a warning until that happens). See IOHCCover::Mode in
cover/iohc_cover.h.

Options list order must stay index-aligned with the Mode enum
(POSITION=0, MY=1, TWO_WAY=2) - IOHCModeSelect::control() casts the selected
index directly to Mode.
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

MODE_OPTIONS = ["Position", "Open / My / Close", "Two-Way (Experimental)"]

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
