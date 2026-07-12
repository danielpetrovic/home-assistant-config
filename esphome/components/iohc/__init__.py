"""Somfy IO (io-homecontrol) radio component.

Thin ESPHome wrapper around a vendored port of
https://github.com/rspaargaren/iohomecontrol (Apache-2.0), itself building on
https://github.com/Velocet/iown-homecontrol's protocol documentation. The
other files in this same directory are the ported radio/protocol files
(flat layout required - see iohc.h for why) - see iohc_board_config.h for pin
mapping and why it deliberately does NOT reuse upstream's own "LILYGO" pin
block (it lists the wrong reset pin for this exact board revision).

All pins/frequencies are fixed in iohc_board_config.h (matching upstream's own
compile-time-constant approach) rather than exposed as YAML options.
"""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@danielpetrovic"]
MULTI_CONF = False

iohc_ns = cg.esphome_ns.namespace("iohc")
IOHCComponent = iohc_ns.class_("IOHCComponent", cg.Component)

CONF_CONTROLLER_ADDRESS = "controller_address"
CONF_SYSTEM_KEY = "system_key"


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


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(IOHCComponent),
        # This bridge's own 2W controller identity (the box/TaHoma-like
        # role, see iohc_controller2w.h) - one shared identity for the whole
        # bridge, unlike each cover's own per-motor node/key. Optional:
        # bonded identity from YAML/secrets.yaml instead of a randomly
        # generated one that only lives in this board's own flash. If a
        # board ever dies, a replacement flashed with the same
        # controller_address/system_key reproduces the exact same identity
        # - no re-bonding needed for any already-2W-bonded motor. Leave
        # unset to keep the original random-generate-and-persist-on-device
        # behavior - this is the current default for every install, since
        # no real 2W bond has yet succeeded on any of them (see
        # io-2w-protocol.md). controller_address = 6 hex chars (3 bytes),
        # system_key = 32 hex chars (16 bytes, AES-128).
        cv.Optional(CONF_CONTROLLER_ADDRESS, default=""): validate_hex_string(6),
        cv.Optional(CONF_SYSTEM_KEY, default=""): cv.sensitive(validate_hex_string(32)),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if config[CONF_CONTROLLER_ADDRESS]:
        cg.add(var.set_fixed_controller_address(config[CONF_CONTROLLER_ADDRESS]))
    if config[CONF_SYSTEM_KEY]:
        cg.add(var.set_fixed_system_key(config[CONF_SYSTEM_KEY]))
