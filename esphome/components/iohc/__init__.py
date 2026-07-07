"""Somfy IO (io-homecontrol) radio component.

Thin ESPHome wrapper around a vendored port of
https://github.com/rspaargaren/iohomecontrol (Apache-2.0), itself building on
https://github.com/Velocet/iown-homecontrol's protocol documentation. The
other files in this same directory are the ported radio/protocol files
(flat layout required - see iohc.h for why) - see iohc_board_config.h for pin
mapping and why it deliberately does NOT reuse upstream's own "LILYGO" pin
block (it lists the wrong reset pin for this exact board revision).

All pins/frequencies are fixed in iohc_board_config.h (matching upstream's own
compile-time-constant approach) rather than exposed as YAML options -
nothing to configure here yet. Phase 0/1 scope: prove real reception on a
single fixed channel, no TX/pairing/hopping.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@danielpetrovic"]
MULTI_CONF = False

iohc_ns = cg.esphome_ns.namespace("iohc")
IOHCComponent = iohc_ns.class_("IOHCComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(IOHCComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
