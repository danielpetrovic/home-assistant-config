"""Somfy IO (io-homecontrol) cover platform.

One IOHCCover = one bonded virtual remote identity (IOHC::IOHCRemote1W),
talking to one physical motor over the 1W-style command layer ported in
iohc_remote1w.h/.cpp - see that file for why 1W (not the harder 2W
challenge/response) is the right target for direct control, and iohc.h for
why this whole component is a flat/subdirectory-per-platform layout.

Position feedback is a local estimate (BlindPosition, travel-time based),
not real motor feedback - hence is_assumed_state(true) in get_traits().
"""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ID

from .. import IOHCComponent, iohc_ns

CODEOWNERS = ["@danielpetrovic"]
DEPENDENCIES = ["iohc"]

CONF_IOHC_ID = "iohc_id"
CONF_TRAVEL_TIME_OPEN = "travel_time_open"
CONF_TRAVEL_TIME_CLOSE = "travel_time_close"
CONF_BROADCAST_TYPE = "broadcast_type"
CONF_MANUFACTURER = "manufacturer"
CONF_NODE = "node"
CONF_KEY = "key"

IOHCCover = iohc_ns.class_("IOHCCover", cover.Cover, cg.Component)


def validate_hex_string(length):
    pattern = re.compile(rf"^[0-9a-fA-F]{{{length}}}$")

    def validator(value):
        value = cv.string_strict(value)
        if value == "":
            return value  # not set - auto-generate and persist on-device instead
        if not pattern.match(value):
            raise cv.Invalid(f"must be exactly {length} hex characters (or empty to auto-generate)")
        return value.lower()

    return validator


CONFIG_SCHEMA = cover.cover_schema(IOHCCover, device_class="shutter").extend(
    {
        cv.GenerateID(CONF_IOHC_ID): cv.use_id(IOHCComponent),
        # Only used by Mode::TIMED (see IOHCCover::Mode) - split into open/
        # close since real motors commonly move at different speeds each
        # direction (gravity helps closing, doesn't help opening), and
        # 230V/high-power motor types move faster than battery ones.
        # Sensible default: 25s, a typical roller shutter full-travel time.
        cv.Optional(CONF_TRAVEL_TIME_OPEN, default="25s"): cv.positive_time_period_seconds,
        cv.Optional(CONF_TRAVEL_TIME_CLOSE, default="25s"): cv.positive_time_period_seconds,
        # Broadcast group the motor listens on (see sDevicesType in
        # iohc_utils.h) - default 0 ("All") matches upstream's own default,
        # but NOT yet confirmed against real hardware for this install. See
        # iohc_remote1w.h for the caveat.
        cv.Optional(CONF_BROADCAST_TYPE, default=0): cv.int_range(min=0, max=15),
        cv.Optional(CONF_MANUFACTURER, default=2): cv.int_range(min=0, max=255),
        # Optional: bonded identity from YAML/secrets.yaml instead of a
        # randomly generated one that only lives in this board's own flash.
        # If a board ever dies, a replacement flashed with the same node/key
        # reproduces the exact same identity - no re-pairing needed. Leave
        # unset to keep the original random-generate-and-persist-on-device
        # behavior. node = 6 hex chars (3 bytes), key = 32 hex chars (16
        # bytes, AES-128).
        cv.Optional(CONF_NODE, default=""): validate_hex_string(6),
        cv.Optional(CONF_KEY, default=""): validate_hex_string(32),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await cover.new_cover(config)
    await cg.register_component(var, config)

    # Deliberately the cover's own YAML component id (e.g. "garden_shutter_io"),
    # not get_object_id() - see the comment on set_nvs_key() in iohc_cover.h for
    # why: this must stay stable across HA-side entity/device naming changes
    # (like adding a device_id for a per-cover sub-device) so it never silently
    # orphans an already-bonded motor's persisted identity/sequence.
    cg.add(var.set_nvs_key(str(config[CONF_ID])))

    parent = await cg.get_variable(config[CONF_IOHC_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_travel_time_open(config[CONF_TRAVEL_TIME_OPEN].total_seconds))
    cg.add(var.set_travel_time_close(config[CONF_TRAVEL_TIME_CLOSE].total_seconds))
    cg.add(var.set_type(config[CONF_BROADCAST_TYPE]))
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    if config[CONF_NODE]:
        cg.add(var.set_fixed_node(config[CONF_NODE]))
    if config[CONF_KEY]:
        cg.add(var.set_fixed_key(config[CONF_KEY]))
