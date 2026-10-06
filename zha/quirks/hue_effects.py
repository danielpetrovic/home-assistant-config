"""Philips Hue lights: Hue effects, plus gradient scenes on the lights that support them."""

import asyncio
import logging
import struct
import types
from pathlib import Path
from typing import Any

import zigpy.device
from zhaquirks.builder import EntityType, QuirkBuilder
from zhaquirks.philips import PHILIPS, SIGNIFY, PhilipsHueLightCluster
from zigpy import types as t
from zigpy.profiles import zha
from zigpy.zcl import foundation
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

_LOGGER = logging.getLogger(__name__)

PHILIPS_MFG_CODE = 0x100B
LIGHT_ENDPOINT_ID = 11

# The Hue capability bits of a light, attribute 0x0001 of the Hue cluster. They read 0x2F on
# the Flux gradient strips, 0x07 on full color lights and 0x05 on a white ambiance light,
# so bit 3 marks a gradient light. A public research on a different gradient strip found the
# same bit.
HUE_CAPABILITIES_NAME = "hue_capabilities"
GRADIENT_CAPABILITY = 0x08

# Seconds to wait before and between the reads of capabilities that are not known yet, and
# how often to try. The lights are not reachable while the radio is starting.
CAPABILITIES_READ_DELAY = 45
CAPABILITIES_READ_ATTEMPTS = 6

# Flags of the multicolor payload, in the order the fields appear on the wire
FLAG_ON_OFF = 0x0001
FLAG_EFFECT_TYPE = 0x0020
FLAG_EFFECT_SPEED = 0x0080

# The strip rejects the speed byte 255 (INVALID_VALUE) in a combined effect command, and
# accepts 252, so 100% is sent as 250. The speed is linear like in Zigbee2MQTT and
# steep at the top: prism is very fast at 200 and strobes at 250, so users pick the
# speed they want
EFFECT_SPEED_MAX_BYTE = 250

ATTR_HUE_EFFECT = 0xF000
ATTR_GRADIENT_SCENE = 0xF001
ATTR_EFFECT_SPEED = 0xF002
ATTR_HUE_CAPABILITIES = 0x0001


def make_enum(name: str, members: dict[str, int]) -> type[t.enum8]:
    """Build an enum8 from names, which a class body cannot do for None, a keyword.

    The names are the options of the select, with underscores shown as spaces.
    """

    def add_members(namespace: dict[str, int]) -> None:
        namespace.update(members)

    return types.new_class(name, (t.enum8,), exec_body=add_members)


# Hue effects of white ambiance lights. The values are the effect types of the Hue
# multicolor command, the effects are listed alphabetically (None first) like the options
# of the select
HueWhiteEffect = make_enum(
    "HueWhiteEffect",
    {
        "None": 0x00,
        "Candle": 0x01,
        "Cosmos": 0x0F,
        "Enchant": 0x11,
        "Glisten": 0x0C,
        "Opal": 0x0B,
        "Sparkle": 0x0A,
        "Sunbeam": 0x10,
        "Sunrise": 0x09,
        "Sunset": 0x0D,
        "Underwater": 0x0E,
    },
)

# Hue effects of full color lights, same as above plus Fireplace and Prism
HueColorEffect = make_enum(
    "HueColorEffect",
    {
        "None": 0x00,
        "Candle": 0x01,
        "Cosmos": 0x0F,
        "Enchant": 0x11,
        "Fireplace": 0x02,
        "Glisten": 0x0C,
        "Opal": 0x0B,
        "Prism": 0x03,
        "Sparkle": 0x0A,
        "Sunbeam": 0x10,
        "Sunrise": 0x09,
        "Sunset": 0x0D,
        "Underwater": 0x0E,
    },
)


# Gradient scenes with the complete multicolor payload of each, sorted by name and copied
# from Zigbee2MQTT. The underscores show as spaces in the select.
GRADIENT_SCENES = {
    "Amber_Bloom": "500104001350000000739d67f2bc7372ec78a0ab78be8a6f2800",
    "Arctic_Aurora": "50010400135000000082548922057511046571c32d5b93192800",
    "Autumn_Gold": "500104001350000000435a7817aa7ba3f979a8a981f3c9852800",
    "Beginnings": "500104001350000000b3474def153e2ad42e98232c7483292800",
    "Blood_Moon": "500104001350000000202a6987c8599ee647ec632779c3142800",
    "Blossom": "50010400135000000039d553d2955ba5287a9f697e25fb802800",
    "Blue_Lagoon": "50010400135000000088c3623975699ea672a0c8831ada6d2800",
    "Blue_Planet": "50010400135000000037a7a3a403b489737b2b746e6873362800",
    "Cancun": "500104001350000000a7eb54673d55944e6265fd6e26bb842800",
    "Chinatown": "500104001350000000b33e5b408e59d90d5b4c6c6360ac792800",
    "City_of_Love": "50010400135000000055830e5cf31b6aa339d2ec70908b802800",
    "Colour_Burst": "500104001350000000f2731ff0c6266a6c64246e57d4f98f2800",
    "Crocus": "50010400135000000050389322f97f2b597343764cc664282800",
    "Crystalline": "5001040013500000006ea96a92a85e58074e18543d9cf3332800",
    "Disturbia": "50010400135000000084f371a4845e6998388c3b4f57ce582800",
    "Emerald_Flutter": "5001040013500000006a933977e34bb0d35e916468f246792800",
    "Emerald_Isle": "500104001350000000e535628dc57ed2667d8b687d1e2a812800",
    "Fairfax": "50010400135000000072d34a3664477d7a61581d5fc08e5b2800",
    "Festive_Fun": "5001040013500000005a9318de53123e9414fdcc67839d612800",
    "First_Light": "500104001350000000b28b7900e959d3f648a614389723362800",
    "Forest_Adventure": "50010400135000000023999bbd76b363d4b674d3415fb3222800",
    "Frosty_Dawn": "5001040013500000006d6883bca87e3029758ec9722d6a722800",
    "Galaxy": "500104001350000000a6cb638b2a4f8cfa549bb9549ff73a2800",
    "Glitz_and_Glam": "500104001350000000cc193cb9b845bad9521d1c77bf6c712800",
    "Golden_Pond": "5001040013500000007e4a88cc4a8605db8728ec7b666c792800",
    "Golden_Star": "5001040013500000007a4a8702eb8372ac7892cd61d51e5c2800",
    "Hal": "50010400135000000075f351a6244cf6dc5d480c658cda862800",
    "Honolulu": "500104001350000000dbfd59866c6378ec6c45cc765c0a822800",
    "Horizon": "500104001350000000488b7d6cbb750c6642f1133cc4033c2800",
    "Ibiza": "500104001350000000014d6d708c73827b7b6c7a8887f98a2800",
    "Lake_Mist": "500104001350000000e3286f39b96859f86266e54ded943f2800",
    "Lake_Placid": "5001040013500000002eab69239a692d996552c54c39743a2800",
    "Lily": "5001040013500000009cfc76c5ab793d4a6a1a9b586b9c522800",
    "Lovebirds": "50010400135000000053ab84ea1a7e35fb7c098c73994c772800",
    "Magneto": "50010400135000000077b3286d9340b9e3662d99943c9b852800",
    "Memento": "500104001350000000f87318a3e31962331ec3532cceea892800",
    "Miami": "50010400135000000022ec61e6d94902d83766c3305a43182800",
    "Midsummer_Sun": "500104001350000000002984799984dd29848eba836c0b7f2800",
    "Midwinter": "500104001350000000bda5532c554dbd254cd5a4428d94392800",
    "Moonlight": "50010400135000000055730e5e9320c1832e96243ebec7652800",
    "Motown": "50010400135000000055730e5db3156623306c533d7a235c2800",
    "Mountain_Breeze": "500104001350000000df843d2355419195465a98674ca97b2800",
    "Narcissa": "500104001350000000b0498a5c0a888fea89eb0b7ee15c742800",
    "Nebula": "50010400135000000026c852e106460d653ee745342964142800",
    "Ocean_Dawn": "5001040013500000005cf9779da97105b96b07485e32564a2800",
    "Orange_Fields": "500104001350000000409c69694c79eafa88498a8fb867aa2800",
    "Osaka": "500104001350000000d649510b5c4deb7c5d8b6d6d2b9b802800",
    "Painted_Sky": "500104001350000000d1c424c3d63783384c3f7a6a83bd6d2800",
    "Palm_Beach": "5001040013500000005ec4679ba56077f85a80ea64639c6a2800",
    "Precious": "5001040013500000007fa8838bb9789a786d7577499a773f2800",
    "Promise": "500104001350000000258b606eca6b28d6382db445df26812800",
    "Resplendent": "500104001350000000278b6d257a58efe84204273a35f5252800",
    "Rio": "500104001350000000a26526088c51a74b58ea6b7137ba892800",
    "Rosy_Sparkle": "500104001350000000810967c63a6cb2aa5ea7094eddd73c2800",
    "Ruby_Romance": "5001040013500000000edb63cbcb6bac0c670b2d58204e572800",
    "Runy_Glow": "50010400135000000095bb53ac2a56eb99591e095c54985e2800",
    "Savanna_Sunset": "50010400135000000005ae65c38c6c6b4b7573ca820fc9832800",
    "Scarlet_Dream": "500104001350000000b02c654e4c5b45ab51fb0950d6c84d2800",
    "Silent_Night": "5001040013500000009e296a245a6f660a75086b70953b6e2800",
    "Smitten": "500104001350000000fe7b70a74b6aa42b65811b60550a592800",
    "Soho": "500104001350000000c52c4e220b6eed8a53d404192b04782800",
    "Spring_Blossom": "500104001350000000a8b75fd0c75826b851a7094d305b652800",
    "Spring_Lake": "5001040013500000004a976d3347736e677561b77a4b07812800",
    "Starlight": "5001040013500000008d897134a9653ec854d2963ed1d4282800",
    "Sunday_Morning": "5001040013500000002c586dc6f87345997c63f983f777892800",
    "Sundown": "500104001350000000f37c68157c6d8efa755ac5512e24332800",
    "Sunflare": "500104001350000000d0aa7d787a7daf197590154d6c14472800",
    "Tokyo": "500104001350000000d1c311665331d3451fd59c4e394c7b2800",
    "Tropical_Twilight": "500104001350000000408523a0b636e777524c0a71a76c6e2800",
    "Tyrell": "500104001350000000ef4419a898370ea84698353574434e2800",
    "Under_the_Tree": "5001040013500000001de498b9a3cc0c9b8563bb6cc1ae5d2800",
    "Valley_Dawn": "500104001350000000c1aa7de03a7a8ce861c7c4410d94412800",
    "Vapor_Wave": "500104001350000000e1c32401251acb183ac31b8051ea842800",
    "Winter_Beauty": "500104001350000000e2335ea7b4942467952db986a7ab7b2800",
    "Winter_Mountain": "5001040013500000002c555c68c55d7c555ef165606136622800",
}


# The scenes are numbered in order, 0 is only the idle state and selecting it does nothing.
# The numbers are only used inside the quirk, the payload of a scene is sent as a whole.
HueGradientScene = make_enum(
    "HueGradientScene",
    {"None": 0, **{name: number for number, name in enumerate(GRADIENT_SCENES, 1)}},
)


# Multicolor payload that stops a running Hue effect
STOP_EFFECT_PAYLOAD = bytes.fromhex("200000")


class HueEffectsCluster(PhilipsHueLightCluster):
    """Hue manufacturer cluster with the effects and scenes as local attributes.

    The lights have no attributes for these, they are all sent as multicolor commands.
    Writing one of the local attributes of a subclass sends the matching command instead
    of an attribute write, so ZHA can expose them as select and number entities.
    """

    # Default values of the local attributes of the subclass, keyed by attribute id
    _DEFAULT_VALUES: dict[int, Any] = {}

    def __init__(self, *args, **kwargs) -> None:
        """Read the capabilities of the light when they are not known yet."""
        super().__init__(*args, **kwargs)
        if HUE_CAPABILITIES_NAME in self.attributes_by_name:
            read = self._read_capabilities()
            try:
                self.create_catching_task(read)
            except RuntimeError:
                # No running event loop, creating the cluster must not fail because of it
                read.close()

    async def _read_capabilities(self) -> None:
        """Read the capabilities of the light unless known, the value is cached for good."""
        for _ in range(CAPABILITIES_READ_ATTEMPTS):
            await asyncio.sleep(CAPABILITIES_READ_DELAY)
            if super().get(HUE_CAPABILITIES_NAME) is not None:
                return
            if self.is_attribute_unsupported(HUE_CAPABILITIES_NAME):
                return
            try:
                await self.read_attributes(
                    [HUE_CAPABILITIES_NAME], manufacturer=PHILIPS_MFG_CODE
                )
            except Exception as err:  # pylint: disable=W0718
                _LOGGER.debug(
                    "Could not read the capabilities of the Hue light: %r", err
                )

    def get(self, key: int | str, default: Any | None = None) -> Any:
        """Return the cached value, falling back to the default of a local attribute."""
        try:
            attr_def = self.find_attribute(key)
        except KeyError:
            return super().get(key, default)

        value = super().get(key)
        if value is None:
            return self._DEFAULT_VALUES.get(attr_def.id, default)
        return value

    async def _send_multicolor(self, payload: bytes) -> foundation.Status:
        """Send a multicolor payload and return the resulting status."""
        result = await self.multicolor(payload, manufacturer=PHILIPS_MFG_CODE)
        return getattr(result, "status", foundation.Status.SUCCESS)

    @staticmethod
    def _speed_byte(speed: int) -> int:
        """Convert an effect speed percentage to the byte the light expects."""
        return round(speed * EFFECT_SPEED_MAX_BYTE / 100)

    def _effect_payload(self, effect: int, speed: int) -> bytes:
        """Build the payload that starts an effect at a speed given as a percentage."""
        if not effect:
            return STOP_EFFECT_PAYLOAD

        flags = FLAG_ON_OFF | FLAG_EFFECT_TYPE | FLAG_EFFECT_SPEED
        return struct.pack("<HBBB", flags, 1, effect, self._speed_byte(speed))

    def _speed_payload(self, speed: int) -> bytes:
        """Build the payload that only changes the speed, it does not turn the light on."""
        return struct.pack("<HB", FLAG_EFFECT_SPEED, self._speed_byte(speed))

    async def _write_local_attribute(
        self, attr_id: int, value: Any
    ) -> foundation.Status:
        """Send the command for a write to a local attribute and update the cache."""
        status = foundation.Status.SUCCESS
        has_scenes = ATTR_GRADIENT_SCENE in self._DEFAULT_VALUES

        if attr_id == ATTR_HUE_EFFECT:
            speed = self.get(ATTR_EFFECT_SPEED)
            status = await self._send_multicolor(
                self._effect_payload(int(value), speed)
            )
            if status == foundation.Status.SUCCESS and has_scenes:
                # A running effect is replaced by the scene and the other way around
                self.update_attribute(ATTR_GRADIENT_SCENE, HueGradientScene["None"])
        elif attr_id == ATTR_GRADIENT_SCENE:
            if value != HueGradientScene["None"]:
                status = await self._send_multicolor(
                    bytes.fromhex(GRADIENT_SCENES[HueGradientScene(value).name])
                )
                if status == foundation.Status.SUCCESS:
                    self.update_attribute(
                        ATTR_HUE_EFFECT, self.AttributeDefs.hue_effect.type["None"]
                    )
        elif attr_id == ATTR_EFFECT_SPEED:
            # A running effect gets the new speed right away, otherwise the speed
            # applies to the next effect that is started
            effect = self.get(ATTR_HUE_EFFECT)
            if effect:
                status = await self._send_multicolor(self._speed_payload(value))
                if status != foundation.Status.SUCCESS:
                    # Start the effect again at the new speed instead
                    status = await self._send_multicolor(
                        self._effect_payload(int(effect), value)
                    )

        if status == foundation.Status.SUCCESS:
            self.update_attribute(attr_id, value)
        return status

    async def write_attributes(
        self,
        attributes: dict[str | int | foundation.ZCLAttributeDef, Any],
        *args,
        **kwargs,
    ) -> list[list[foundation.WriteAttributesStatusRecord]]:
        """Turn writes to the local attributes into multicolor commands."""
        local: dict[int, Any] = {}
        remote: dict[str | int | foundation.ZCLAttributeDef, Any] = {}
        for attribute, value in attributes.items():
            attr_def = self.find_attribute(attribute)
            if attr_def.id in self._DEFAULT_VALUES:
                local[attr_def.id] = attr_def.type(value)
            else:
                remote[attribute] = value

        if not local:
            return await super().write_attributes(attributes, *args, **kwargs)

        records: list[foundation.WriteAttributesStatusRecord] = []
        if remote:
            result = await super().write_attributes(remote, *args, **kwargs)
            records.extend(result[0])

        for attr_id, value in local.items():
            status = await self._write_local_attribute(attr_id, value)
            records.append(foundation.WriteAttributesStatusRecord(status, attr_id))

        return [records]


class HueWhiteEffectsCluster(HueEffectsCluster):
    """Effects of white ambiance lights."""

    # pylint: disable=R0903
    class AttributeDefs(BaseAttributeDefs):
        """Local attributes, never read from or written to the device."""

        hue_effect = ZCLAttributeDef(
            id=ATTR_HUE_EFFECT, type=HueWhiteEffect, access="rw"
        )
        # Percentage
        effect_speed = ZCLAttributeDef(
            id=ATTR_EFFECT_SPEED, type=t.uint8_t, access="rw"
        )

    _DEFAULT_VALUES = {ATTR_HUE_EFFECT: HueWhiteEffect["None"], ATTR_EFFECT_SPEED: 50}


class HueColorEffectsCluster(HueEffectsCluster):
    """Effects of full color lights."""

    # pylint: disable=R0903
    class AttributeDefs(BaseAttributeDefs):
        """Local attributes, except the capabilities which are read from the light."""

        hue_effect = ZCLAttributeDef(
            id=ATTR_HUE_EFFECT, type=HueColorEffect, access="rw"
        )
        # Percentage
        effect_speed = ZCLAttributeDef(
            id=ATTR_EFFECT_SPEED, type=t.uint8_t, access="rw"
        )
        hue_capabilities = ZCLAttributeDef(
            id=ATTR_HUE_CAPABILITIES,
            type=t.bitmap32,
            access="r",
            manufacturer_code=PHILIPS_MFG_CODE,
        )

    _DEFAULT_VALUES = {ATTR_HUE_EFFECT: HueColorEffect["None"], ATTR_EFFECT_SPEED: 50}


class HueGradientEffectsCluster(HueColorEffectsCluster):
    """Effects of full color lights and the gradient scenes of gradient lights."""

    # pylint: disable=R0903
    class AttributeDefs(HueColorEffectsCluster.AttributeDefs):
        """Local attributes, except the capabilities which are read from the light."""

        gradient_scene = ZCLAttributeDef(
            id=ATTR_GRADIENT_SCENE, type=HueGradientScene, access="rw"
        )

    _DEFAULT_VALUES = {
        ATTR_HUE_EFFECT: HueColorEffect["None"],
        ATTR_GRADIENT_SCENE: HueGradientScene["None"],
        ATTR_EFFECT_SPEED: 50,
    }


def _is_hue_light(device: zigpy.device.Device, device_type: zha.DeviceType) -> bool:
    """Return True for a light of the device type that has the Hue cluster."""
    try:
        endpoint = device.endpoints[LIGHT_ENDPOINT_ID]
        return (
            endpoint.profile_id == zha.PROFILE_ID
            and endpoint.device_type == device_type
            and PhilipsHueLightCluster.cluster_id in endpoint.in_clusters
        )
    except Exception:  # pylint: disable=W0718
        # A filter must never stop a device from joining
        return False


def is_color_light(device: zigpy.device.Device) -> bool:
    """Return True for a full color Hue light."""
    return _is_hue_light(device, zha.DeviceType.EXTENDED_COLOR_LIGHT)


def is_white_light(device: zigpy.device.Device) -> bool:
    """Return True for a white ambiance Hue light, which has no color."""
    return _is_hue_light(device, zha.DeviceType.COLOR_TEMPERATURE_LIGHT)


def is_gradient_light(device: zigpy.device.Device) -> bool:
    """Return True for a full color Hue light that is known to have the gradient capability.

    The capability is cached after it has been read from the light, and zigpy loads the
    cache before it applies the quirks. A light that has never been read is not a
    gradient light yet, it becomes one the next time it is loaded.
    """
    try:
        if not is_color_light(device):
            return False

        cluster = device.endpoints[LIGHT_ENDPOINT_ID].in_clusters[
            PhilipsHueLightCluster.cluster_id
        ]
        capabilities = cluster._attr_cache.get(  # pylint: disable=W0212
            ATTR_HUE_CAPABILITIES
        )
        return capabilities is not None and bool(capabilities & GRADIENT_CAPABILITY)
    except Exception:  # pylint: disable=W0718
        # A filter must never stop a device from joining
        return False


# The cluster, the effects and the filter of each kind of light
WHITE = (HueWhiteEffectsCluster, HueWhiteEffect, is_white_light)
COLOR = (HueColorEffectsCluster, HueColorEffect, is_color_light)
GRADIENT = (HueGradientEffectsCluster, HueColorEffect, is_gradient_light)

# The file of the upstream quirks that apply the Hue cluster to the Hue lights
UPSTREAM_HUE_QUIRK = ("philips", "hue_light.py")


def add_hue_quirk(
    models: list[tuple[str, str | None]],
    kind: tuple[type[HueEffectsCluster], type[t.enum8], Any],
    friendly_name: str | None = None,
) -> None:
    """Register the effects quirk for manufacturer and model pairs, a model can be None."""
    cluster, effect_enum, light_filter = kind
    builder = QuirkBuilder().filter(light_filter)
    for manufacturer, model in models:
        builder = builder.applies_to(manufacturer, model)

    # Models that have a name in the upstream quirks keep it, the others keep the model
    if friendly_name:
        builder = builder.friendly_name(model=friendly_name, manufacturer=PHILIPS)

    builder = builder.replaces(cluster, endpoint_id=LIGHT_ENDPOINT_ID).enum(
        cluster.AttributeDefs.hue_effect.name,
        effect_enum,
        cluster.cluster_id,
        endpoint_id=LIGHT_ENDPOINT_ID,
        entity_type=EntityType.STANDARD,
        translation_key="hue_effect",
        fallback_name="Hue effect",
    )

    if ATTR_GRADIENT_SCENE in cluster._DEFAULT_VALUES:  # pylint: disable=W0212
        builder = builder.enum(
            cluster.AttributeDefs.gradient_scene.name,
            HueGradientScene,
            cluster.cluster_id,
            endpoint_id=LIGHT_ENDPOINT_ID,
            entity_type=EntityType.STANDARD,
            translation_key="gradient_scene",
            fallback_name="Gradient scene",
        )

    builder.number(
        cluster.AttributeDefs.effect_speed.name,
        cluster.cluster_id,
        endpoint_id=LIGHT_ENDPOINT_ID,
        min_value=0,
        max_value=100,
        step=10,
        unit="%",
        entity_type=EntityType.STANDARD,
        translation_key="effect_speed",
        fallback_name="Effect speed",
    ).add_to_registry()


def upstream_hue_models() -> dict[str | None, list[tuple[str, str]]]:
    """Return the models of the upstream Hue light quirks, grouped by friendly name."""
    groups: dict[str | None, list[tuple[str, str]]] = {}
    try:
        from zha.quirks import DEVICE_REGISTRY  # pylint: disable=C0415

        for key, entries in list(DEVICE_REGISTRY._registry.items()):  # pylint: disable=W0212
            if key.manufacturer is None or key.model is None:
                continue

            for entry in entries:
                source = entry.source.file
                if source and Path(source).parts[-2:] == UPSTREAM_HUE_QUIRK:
                    definition = getattr(
                        entry.zha_device_factory, "quirk_definition", None
                    )
                    name = (
                        definition.friendly_name.model
                        if definition and definition.friendly_name
                        else None
                    )
                    groups.setdefault(name, []).append((key.manufacturer, key.model))
                    break
    except Exception:  # pylint: disable=W0718
        _LOGGER.warning(
            "Could not read the upstream Hue light quirks, the Hue effects are only added "
            "to Hue lights that have no upstream quirk",
            exc_info=True,
        )

    return groups


# Any Hue light that has the Hue cluster, recognized by its device type
ANY_HUE_LIGHT = [(PHILIPS, None), (SIGNIFY, None)]
add_hue_quirk(ANY_HUE_LIGHT, WHITE)
add_hue_quirk(ANY_HUE_LIGHT, COLOR)
# The gradient lights are registered after the color lights, so they win
add_hue_quirk(ANY_HUE_LIGHT, GRADIENT)

# Exact models are matched before the entries above, so the models of the upstream quirks
# need their own entries, which also keep the names that the upstream quirks give them
for upstream_name, upstream_models in upstream_hue_models().items():
    add_hue_quirk(upstream_models, WHITE, upstream_name)
    add_hue_quirk(upstream_models, COLOR, upstream_name)
    add_hue_quirk(upstream_models, GRADIENT, upstream_name)
