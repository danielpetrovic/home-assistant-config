"""Niko Connected socket outlet (170-33505 / 170-34605)."""

from zigpy import types as t
from zigpy.zcl.clusters.homeautomation import ElectricalMeasurement
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

from zhaquirks.builder import EntityType, QuirkBuilder
from zhaquirks.clusters import CustomCluster

NIKO = "NIKO NV"
NIKO_MFG_CODE = 0x125F


class LedAlertColor(t.enum24):
    """Colour the status LED shows briefly as an alert."""

    Off = 0x000000
    White = 0x0000FF
    Blue = 0x00FF00
    Red = 0xFF0000
    Purple = 0xFFFFFF


class NikoOutletConfigCluster(CustomCluster):
    """Niko manufacturer specific configuration cluster of the outlet."""

    cluster_id = 0xFC00
    ep_attribute = "niko_config"

    # pylint: disable=R0903
    class AttributeDefs(BaseAttributeDefs):
        """Attributes of the outlet configuration cluster."""

        # Alert colour of the status LED. The device shows it for a few seconds and
        # then returns to its normal colour for the relay state (white when on).
        led_alert_color = ZCLAttributeDef(
            id=0x0100,
            type=LedAlertColor,
            access="rw",
            manufacturer_code=NIKO_MFG_CODE,
        )
        # 0 = locked (physical button disabled), 1 = unlocked (default)
        child_lock = ZCLAttributeDef(
            id=0x0101,
            type=t.uint8_t,
            access="rw",
            manufacturer_code=NIKO_MFG_CODE,
        )
        # 0 = status LED disabled, 1 = status LED enabled (default)
        led_enable = ZCLAttributeDef(
            id=0x0104,
            type=t.uint8_t,
            access="rw",
            manufacturer_code=NIKO_MFG_CODE,
        )

    async def bind(self):
        """Bind cluster and pre-load attributes."""
        self.create_catching_task(
            self.read_attributes([attr.name for attr in self.AttributeDefs])
        )
        return await super().bind()


(
    QuirkBuilder(NIKO, "Connected socket outlet")
    .replaces(NikoOutletConfigCluster)
    .switch(
        NikoOutletConfigCluster.AttributeDefs.child_lock.name,
        NikoOutletConfigCluster.cluster_id,
        off_value=1,
        on_value=0,
        entity_type=EntityType.CONFIG,
        translation_key="child_lock",
        fallback_name="Child lock",
    )
    .switch(
        NikoOutletConfigCluster.AttributeDefs.led_enable.name,
        NikoOutletConfigCluster.cluster_id,
        entity_type=EntityType.CONFIG,
        translation_key="led_indicator",
        fallback_name="LED indicator",
    )
    .enum(
        NikoOutletConfigCluster.AttributeDefs.led_alert_color.name,
        LedAlertColor,
        NikoOutletConfigCluster.cluster_id,
        entity_type=EntityType.CONFIG,
        initially_disabled=True,
        translation_key="led_indicator_alert_color",
        fallback_name="LED indicator alert colour",
    )
    # The socket reports voltage and current every few seconds with a small jitter,
    # which floods the recorder, so they are disabled by default. Power and energy stay
    # enabled.
    .change_entity_metadata(
        endpoint_id=1,
        cluster_id=ElectricalMeasurement.cluster_id,
        unique_id_suffix="rms_voltage",
        new_entity_registry_enabled_default=False,
    )
    .change_entity_metadata(
        endpoint_id=1,
        cluster_id=ElectricalMeasurement.cluster_id,
        unique_id_suffix="rms_current",
        new_entity_registry_enabled_default=False,
    )
    .add_to_registry()
)
