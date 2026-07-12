"""Somfy IO (io-homecontrol) per-cover diagnostic sensors.

Two `type:` options, both standalone (not tied to the cover entity's own
position/state):

- `target_closure` (default, for backward compatibility with existing YAML
  that omits `type:`): the real, motor-reported closure percentage decoded
  from passively overheard 2W traffic - see IOHCCover::update_real_position()
  in cover/iohc_cover.cpp and README's "Real position feedback" section for
  how this data arrives and what it requires (an existing 2W-bonded
  controller, e.g. TaHoma, already active on the installation - this bridge
  cannot query it alone). Deliberately matches Home Assistant's Overkiz
  integration's own "Target closure" sensor convention exactly (0=open,
  100=closed - the OPPOSITE of this cover's own `position` attribute, which
  follows HA's standard cover convention of 100=open/0=closed) so the two are
  directly comparable. Only ever publishes once real 2W traffic for this
  cover's motor_address has actually been overheard - starts unavailable
  (matching Overkiz's own sensor's behavior on a shutter that's never been
  polled) if none has arrived yet.

- `rssi`: THIS bridge's own radio's signal strength for the last frame
  received from this cover's motor_address - any frame, 1W or 2W, decoded or
  not. Distinct from Overkiz's own "RSSI Level"/"Discrete RSSI Level"
  sensors, which reflect TaHoma's radio, not this board's - see
  IOHCCover::update_last_rssi() in cover/iohc_cover.cpp. Requires
  motor_address to be set on the cover (same requirement as
  target_closure) - without it, this bridge has no way to attribute a
  received frame's RSSI to a specific cover.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_PERCENT,
)

from ..cover import IOHCCover

CODEOWNERS = ["@danielpetrovic"]
DEPENDENCIES = ["iohc"]

CONF_COVER_ID = "cover_id"

TYPE_TARGET_CLOSURE = "target_closure"
TYPE_RSSI = "rssi"

SENSOR_SCHEMAS = {
    TYPE_TARGET_CLOSURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    TYPE_RSSI: sensor.sensor_schema(
        unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        sensor_type: schema.extend(
            {
                cv.Required(CONF_COVER_ID): cv.use_id(IOHCCover),
            }
        )
        for sensor_type, schema in SENSOR_SCHEMAS.items()
    },
    default_type=TYPE_TARGET_CLOSURE,
)


async def to_code(config):
    var = await sensor.new_sensor(config)

    cover_var = await cg.get_variable(config[CONF_COVER_ID])
    if config[CONF_TYPE] == TYPE_RSSI:
        cg.add(cover_var.set_last_rssi_sensor(var))
    else:
        cg.add(cover_var.set_target_closure_sensor(var))
