"""Philips Hue lights: Hue effects, plus gradient scenes on the gradient strips."""

import struct
from typing import Any

from zigpy import types as t
from zigpy.zcl import foundation
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

from zhaquirks.builder import EntityType, QuirkBuilder
from zhaquirks.philips import PHILIPS, SIGNIFY, PhilipsHueLightCluster

PHILIPS_MFG_CODE = 0x100B

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


class HueWhiteEffect(t.enum8):
    """Hue effects of white ambiance lights, values as used by the multicolor command."""

    none = 0x00
    candle = 0x01
    sunrise = 0x09
    sparkle = 0x0A
    opal = 0x0B
    glisten = 0x0C
    sunset = 0x0D
    underwater = 0x0E
    cosmos = 0x0F
    sunbeam = 0x10
    enchant = 0x11


class HueColorEffect(t.enum8):
    """Hue effects of full color lights, values as used by the multicolor command."""

    none = 0x00
    candle = 0x01
    fireplace = 0x02
    prism = 0x03
    sunrise = 0x09
    sparkle = 0x0A
    opal = 0x0B
    glisten = 0x0C
    sunset = 0x0D
    underwater = 0x0E
    cosmos = 0x0F
    sunbeam = 0x10
    enchant = 0x11


class HueGradientScene(t.enum8):
    """Hue gradient scenes. None is only the idle state, selecting it does nothing."""

    none = 0
    blossom = 1
    crocus = 2
    precious = 3
    narcissa = 4
    beginnings = 5
    first_light = 6
    horizon = 7
    valley_dawn = 8
    sunflare = 9
    emerald_flutter = 10
    memento = 11
    resplendent = 12
    scarlet_dream = 13
    lovebirds = 14
    smitten = 15
    glitz_and_glam = 16
    promise = 17
    ruby_romance = 18
    city_of_love = 19
    honolulu = 20
    savanna_sunset = 21
    golden_pond = 22
    runy_glow = 23
    tropical_twilight = 24
    miami = 25
    cancun = 26
    rio = 27
    chinatown = 28
    ibiza = 29
    osaka = 30
    tokyo = 31
    motown = 32
    fairfax = 33
    galaxy = 34
    starlight = 35
    artic_aurora = 36
    moonlight = 37
    nebula = 38
    sundown = 39
    blue_lagoon = 40
    palm_beach = 41
    lake_placid = 42
    mountain_breeze = 43
    lake_mist = 44
    ocean_dawn = 45
    frosty_dawn = 46
    sunday_morning = 47
    emerald_isle = 48
    spring_blossom = 49
    midsummer_sun = 50
    autumn_gold = 51
    spring_lake = 52
    winter_mountain = 53
    midwinter = 54
    amber_bloom = 55
    lily = 56
    painted_sky = 57
    winter_beauty = 58
    orange_fields = 59
    forest_adventure = 60
    blue_planet = 61
    soho = 62
    vapor_wave = 63
    magneto = 64
    tyrell = 65
    disturbia = 66
    hal = 67
    golden_star = 68
    under_the_tree = 69
    silent_night = 70
    rosy_sparkle = 71
    festive_fun = 72
    colour_burst = 73
    crystalline = 74


# Complete multicolor payloads of the gradient scenes
GRADIENT_SCENE_PAYLOADS = {
    HueGradientScene.blossom: "50010400135000000039d553d2955ba5287a9f697e25fb802800",
    HueGradientScene.crocus: "50010400135000000050389322f97f2b597343764cc664282800",
    HueGradientScene.precious: "5001040013500000007fa8838bb9789a786d7577499a773f2800",
    HueGradientScene.narcissa: "500104001350000000b0498a5c0a888fea89eb0b7ee15c742800",
    HueGradientScene.beginnings: "500104001350000000b3474def153e2ad42e98232c7483292800",
    HueGradientScene.first_light: "500104001350000000b28b7900e959d3f648a614389723362800",
    HueGradientScene.horizon: "500104001350000000488b7d6cbb750c6642f1133cc4033c2800",
    HueGradientScene.valley_dawn: "500104001350000000c1aa7de03a7a8ce861c7c4410d94412800",
    HueGradientScene.sunflare: "500104001350000000d0aa7d787a7daf197590154d6c14472800",
    HueGradientScene.emerald_flutter: "5001040013500000006a933977e34bb0d35e916468f246792800",
    HueGradientScene.memento: "500104001350000000f87318a3e31962331ec3532cceea892800",
    HueGradientScene.resplendent: "500104001350000000278b6d257a58efe84204273a35f5252800",
    HueGradientScene.scarlet_dream: "500104001350000000b02c654e4c5b45ab51fb0950d6c84d2800",
    HueGradientScene.lovebirds: "50010400135000000053ab84ea1a7e35fb7c098c73994c772800",
    HueGradientScene.smitten: "500104001350000000fe7b70a74b6aa42b65811b60550a592800",
    HueGradientScene.glitz_and_glam: "500104001350000000cc193cb9b845bad9521d1c77bf6c712800",
    HueGradientScene.promise: "500104001350000000258b606eca6b28d6382db445df26812800",
    HueGradientScene.ruby_romance: "5001040013500000000edb63cbcb6bac0c670b2d58204e572800",
    HueGradientScene.city_of_love: "50010400135000000055830e5cf31b6aa339d2ec70908b802800",
    HueGradientScene.honolulu: "500104001350000000dbfd59866c6378ec6c45cc765c0a822800",
    HueGradientScene.savanna_sunset: "50010400135000000005ae65c38c6c6b4b7573ca820fc9832800",
    HueGradientScene.golden_pond: "5001040013500000007e4a88cc4a8605db8728ec7b666c792800",
    HueGradientScene.runy_glow: "50010400135000000095bb53ac2a56eb99591e095c54985e2800",
    HueGradientScene.tropical_twilight: "500104001350000000408523a0b636e777524c0a71a76c6e2800",
    HueGradientScene.miami: "50010400135000000022ec61e6d94902d83766c3305a43182800",
    HueGradientScene.cancun: "500104001350000000a7eb54673d55944e6265fd6e26bb842800",
    HueGradientScene.rio: "500104001350000000a26526088c51a74b58ea6b7137ba892800",
    HueGradientScene.chinatown: "500104001350000000b33e5b408e59d90d5b4c6c6360ac792800",
    HueGradientScene.ibiza: "500104001350000000014d6d708c73827b7b6c7a8887f98a2800",
    HueGradientScene.osaka: "500104001350000000d649510b5c4deb7c5d8b6d6d2b9b802800",
    HueGradientScene.tokyo: "500104001350000000d1c311665331d3451fd59c4e394c7b2800",
    HueGradientScene.motown: "50010400135000000055730e5db3156623306c533d7a235c2800",
    HueGradientScene.fairfax: "50010400135000000072d34a3664477d7a61581d5fc08e5b2800",
    HueGradientScene.galaxy: "500104001350000000a6cb638b2a4f8cfa549bb9549ff73a2800",
    HueGradientScene.starlight: "5001040013500000008d897134a9653ec854d2963ed1d4282800",
    HueGradientScene.artic_aurora: "50010400135000000082548922057511046571c32d5b93192800",
    HueGradientScene.moonlight: "50010400135000000055730e5e9320c1832e96243ebec7652800",
    HueGradientScene.nebula: "50010400135000000026c852e106460d653ee745342964142800",
    HueGradientScene.sundown: "500104001350000000f37c68157c6d8efa755ac5512e24332800",
    HueGradientScene.blue_lagoon: "50010400135000000088c3623975699ea672a0c8831ada6d2800",
    HueGradientScene.palm_beach: "5001040013500000005ec4679ba56077f85a80ea64639c6a2800",
    HueGradientScene.lake_placid: "5001040013500000002eab69239a692d996552c54c39743a2800",
    HueGradientScene.mountain_breeze: "500104001350000000df843d2355419195465a98674ca97b2800",
    HueGradientScene.lake_mist: "500104001350000000e3286f39b96859f86266e54ded943f2800",
    HueGradientScene.ocean_dawn: "5001040013500000005cf9779da97105b96b07485e32564a2800",
    HueGradientScene.frosty_dawn: "5001040013500000006d6883bca87e3029758ec9722d6a722800",
    HueGradientScene.sunday_morning: "5001040013500000002c586dc6f87345997c63f983f777892800",
    HueGradientScene.emerald_isle: "500104001350000000e535628dc57ed2667d8b687d1e2a812800",
    HueGradientScene.spring_blossom: "500104001350000000a8b75fd0c75826b851a7094d305b652800",
    HueGradientScene.midsummer_sun: "500104001350000000002984799984dd29848eba836c0b7f2800",
    HueGradientScene.autumn_gold: "500104001350000000435a7817aa7ba3f979a8a981f3c9852800",
    HueGradientScene.spring_lake: "5001040013500000004a976d3347736e677561b77a4b07812800",
    HueGradientScene.winter_mountain: "5001040013500000002c555c68c55d7c555ef165606136622800",
    HueGradientScene.midwinter: "500104001350000000bda5532c554dbd254cd5a4428d94392800",
    HueGradientScene.amber_bloom: "500104001350000000739d67f2bc7372ec78a0ab78be8a6f2800",
    HueGradientScene.lily: "5001040013500000009cfc76c5ab793d4a6a1a9b586b9c522800",
    HueGradientScene.painted_sky: "500104001350000000d1c424c3d63783384c3f7a6a83bd6d2800",
    HueGradientScene.winter_beauty: "500104001350000000e2335ea7b4942467952db986a7ab7b2800",
    HueGradientScene.orange_fields: "500104001350000000409c69694c79eafa88498a8fb867aa2800",
    HueGradientScene.forest_adventure: "50010400135000000023999bbd76b363d4b674d3415fb3222800",
    HueGradientScene.blue_planet: "50010400135000000037a7a3a403b489737b2b746e6873362800",
    HueGradientScene.soho: "500104001350000000c52c4e220b6eed8a53d404192b04782800",
    HueGradientScene.vapor_wave: "500104001350000000e1c32401251acb183ac31b8051ea842800",
    HueGradientScene.magneto: "50010400135000000077b3286d9340b9e3662d99943c9b852800",
    HueGradientScene.tyrell: "500104001350000000ef4419a898370ea84698353574434e2800",
    HueGradientScene.disturbia: "50010400135000000084f371a4845e6998388c3b4f57ce582800",
    HueGradientScene.hal: "50010400135000000075f351a6244cf6dc5d480c658cda862800",
    HueGradientScene.golden_star: "5001040013500000007a4a8702eb8372ac7892cd61d51e5c2800",
    HueGradientScene.under_the_tree: "5001040013500000001de498b9a3cc0c9b8563bb6cc1ae5d2800",
    HueGradientScene.silent_night: "5001040013500000009e296a245a6f660a75086b70953b6e2800",
    HueGradientScene.rosy_sparkle: "500104001350000000810967c63a6cb2aa5ea7094eddd73c2800",
    HueGradientScene.festive_fun: "5001040013500000005a9318de53123e9414fdcc67839d612800",
    HueGradientScene.colour_burst: "500104001350000000f2731ff0c6266a6c64246e57d4f98f2800",
    HueGradientScene.crystalline: "5001040013500000006ea96a92a85e58074e18543d9cf3332800",
}

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

    async def _write_local_attribute(self, attr_id: int, value: Any) -> foundation.Status:
        """Send the command for a write to a local attribute and update the cache."""
        status = foundation.Status.SUCCESS
        has_scenes = ATTR_GRADIENT_SCENE in self._DEFAULT_VALUES

        if attr_id == ATTR_HUE_EFFECT:
            speed = self.get(ATTR_EFFECT_SPEED)
            status = await self._send_multicolor(self._effect_payload(int(value), speed))
            if status == foundation.Status.SUCCESS and has_scenes:
                # A running effect is replaced by the scene and the other way around
                self.update_attribute(ATTR_GRADIENT_SCENE, HueGradientScene.none)
        elif attr_id == ATTR_GRADIENT_SCENE:
            if value != HueGradientScene.none:
                status = await self._send_multicolor(
                    bytes.fromhex(GRADIENT_SCENE_PAYLOADS[HueGradientScene(value)])
                )
                if status == foundation.Status.SUCCESS:
                    self.update_attribute(
                        ATTR_HUE_EFFECT, self.AttributeDefs.hue_effect.type.none
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

        hue_effect = ZCLAttributeDef(id=ATTR_HUE_EFFECT, type=HueWhiteEffect, access="rw")
        # Percentage
        effect_speed = ZCLAttributeDef(id=ATTR_EFFECT_SPEED, type=t.uint8_t, access="rw")

    _DEFAULT_VALUES = {ATTR_HUE_EFFECT: HueWhiteEffect.none, ATTR_EFFECT_SPEED: 50}


class HueColorEffectsCluster(HueEffectsCluster):
    """Effects of full color lights."""

    # pylint: disable=R0903
    class AttributeDefs(BaseAttributeDefs):
        """Local attributes, never read from or written to the device."""

        hue_effect = ZCLAttributeDef(id=ATTR_HUE_EFFECT, type=HueColorEffect, access="rw")
        # Percentage
        effect_speed = ZCLAttributeDef(id=ATTR_EFFECT_SPEED, type=t.uint8_t, access="rw")

    _DEFAULT_VALUES = {ATTR_HUE_EFFECT: HueColorEffect.none, ATTR_EFFECT_SPEED: 50}


class HueGradientEffectsCluster(HueEffectsCluster):
    """Effects and gradient scenes of gradient lights."""

    # pylint: disable=R0903
    class AttributeDefs(BaseAttributeDefs):
        """Local attributes, never read from or written to the device."""

        hue_effect = ZCLAttributeDef(id=ATTR_HUE_EFFECT, type=HueColorEffect, access="rw")
        gradient_scene = ZCLAttributeDef(
            id=ATTR_GRADIENT_SCENE, type=HueGradientScene, access="rw"
        )
        # Percentage
        effect_speed = ZCLAttributeDef(id=ATTR_EFFECT_SPEED, type=t.uint8_t, access="rw")

    _DEFAULT_VALUES = {
        ATTR_HUE_EFFECT: HueColorEffect.none,
        ATTR_GRADIENT_SCENE: HueGradientScene.none,
        ATTR_EFFECT_SPEED: 50,
    }


def add_hue_quirk(
    models: list[str],
    cluster: type[HueEffectsCluster],
    effect_enum: type[t.enum8],
    friendly_name: str | None = None,
) -> None:
    """Register the effects quirk for models that report as either manufacturer."""
    builder = QuirkBuilder()
    for model in models:
        builder = builder.applies_to(PHILIPS, model).applies_to(SIGNIFY, model)

    # Models that have a name in the upstream quirks keep it, the others keep the model
    if friendly_name:
        builder = builder.friendly_name(model=friendly_name, manufacturer=PHILIPS)

    builder = builder.replaces(cluster, endpoint_id=11).enum(
        cluster.AttributeDefs.hue_effect.name,
        effect_enum,
        cluster.cluster_id,
        endpoint_id=11,
        entity_type=EntityType.STANDARD,
        translation_key="hue_effect",
        fallback_name="Hue effect",
    )

    if ATTR_GRADIENT_SCENE in cluster._DEFAULT_VALUES:  # pylint: disable=W0212
        builder = builder.enum(
            cluster.AttributeDefs.gradient_scene.name,
            HueGradientScene,
            cluster.cluster_id,
            endpoint_id=11,
            entity_type=EntityType.STANDARD,
            translation_key="gradient_scene",
            fallback_name="Gradient scene",
        )

    builder.number(
        cluster.AttributeDefs.effect_speed.name,
        cluster.cluster_id,
        endpoint_id=11,
        min_value=0,
        max_value=100,
        step=10,
        unit="%",
        entity_type=EntityType.STANDARD,
        translation_key="effect_speed",
        fallback_name="Effect speed",
    ).add_to_registry()


# Hue white ambiance lights (color temperature only)
add_hue_quirk(
    ["3216131P6", "929003099201"],
    HueWhiteEffectsCluster,
    HueWhiteEffect,
)

# Hue full color lights without a name in the upstream quirks
add_hue_quirk(
    [
        "1742030P7",
        "1742330P7",
        "1743830P7",
        "1746130P7",
        "5062231P7",
        "929003810001_01",
        "929003810001_02",
        "929004297402",
        "LCG002",
        "LCL001",
    ],
    HueColorEffectsCluster,
    HueColorEffect,
)

# Hue full color lights with a name in the upstream quirks
add_hue_quirk(
    ["1743530P7"],
    HueColorEffectsCluster,
    HueColorEffect,
    "Hue Discover outdoor floodlight",
)
add_hue_quirk(
    ["1746330P7"],
    HueColorEffectsCluster,
    HueColorEffect,
    "Hue Appear Outdoor wall light",
)
add_hue_quirk(
    ["LCL008"],
    HueColorEffectsCluster,
    HueColorEffect,
    "Hue Lightstrip Solo",
)

# Hue gradient lights
add_hue_quirk(
    ["929004610402"],
    HueGradientEffectsCluster,
    HueColorEffect,
    "Hue Flux gradient lightstrip",
)
