"""Somfy io-homecontrol per-cover travel time number.

Runtime-adjustable Travel Time Open/Close, only meaningful in the cover's
"1W Timed" mode (see IOHCCover::Mode) - the YAML travel_time_open/
travel_time_close fields on the cover platform are just the initial default;
changing this number entity persists the new value via Preferences (see
IOHCCover::set_travel_time_open_and_persist()), so it survives a reboot
without needing a reflash.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import ENTITY_CATEGORY_CONFIG

from .. import iohc_ns
from ..cover import IOHCCover

CODEOWNERS = ["@danielpetrovic"]
DEPENDENCIES = ["iohc"]

CONF_COVER_ID = "cover_id"
CONF_TYPE = "type"

IOHCTravelTimeNumber = iohc_ns.class_("IOHCTravelTimeNumber", number.Number, cg.Component)

# Nested enum on IOHCTravelTimeNumber itself (unlike button's IOHC::RemoteButton,
# which lives in a separate global namespace shared with iohc_remote1w.h).
TravelTimeType = IOHCTravelTimeNumber.enum("Type", is_class=True)
TYPES = {
    "open": TravelTimeType.OPEN,
    "close": TravelTimeType.CLOSE,
}

CONFIG_SCHEMA = number.number_schema(
    IOHCTravelTimeNumber,
    unit_of_measurement="s",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend(
    {
        cv.Required(CONF_COVER_ID): cv.use_id(IOHCCover),
        cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await number.new_number(config, min_value=1, max_value=300, step=1)
    await cg.register_component(var, config)

    cover_var = await cg.get_variable(config[CONF_COVER_ID])
    cg.add(var.set_cover(cover_var))
    cg.add(var.set_travel_time_type(config[CONF_TYPE]))
