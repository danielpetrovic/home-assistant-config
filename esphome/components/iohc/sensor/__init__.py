"""Somfy IO (io-homecontrol) per-cover target closure sensor.

Standalone numeric sensor (not tied to the cover entity's own position/state)
showing the real, motor-reported closure percentage decoded from passively
overheard 2W traffic - see IOHCCover::update_real_position() in
cover/iohc_cover.cpp and README's "Real position feedback" section for how
this data arrives and what it requires (an existing 2W-bonded controller,
e.g. TaHoma, already active on the installation - this bridge cannot query
it alone).

Deliberately matches Home Assistant's Overkiz integration's own "Target
closure" sensor convention exactly (0=open, 100=closed - the OPPOSITE of
this cover's own `position` attribute, which follows HA's standard cover
convention of 100=open/0=closed) so the two are directly comparable, and so
this sensor works standalone in automations without needing the cover
entity's own model of state at all - e.g. for anyone who wants a plain
numeric trigger condition instead of parsing cover position/state.

Only ever publishes a value once real 2W traffic for this cover's
motor_address has actually been overheard - starts unavailable (matching
Overkiz's own sensor's behavior on a shutter that's never been polled) if
none has arrived yet.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import STATE_CLASS_MEASUREMENT, UNIT_PERCENT

from ..cover import IOHCCover

CODEOWNERS = ["@danielpetrovic"]
DEPENDENCIES = ["iohc"]

CONF_COVER_ID = "cover_id"

CONFIG_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_PERCENT,
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.Required(CONF_COVER_ID): cv.use_id(IOHCCover),
    }
)


async def to_code(config):
    var = await sensor.new_sensor(config)

    cover_var = await cg.get_variable(config[CONF_COVER_ID])
    cg.add(cover_var.set_target_closure_sensor(var))
