"""Aqara Smart Pet Feeder C1 (aqara.feeder.acn001) with an on-device feeding schedule.

ZHA gives the feeder a feed button, mode, serving size, portion weight, child lock,
LED indicator and the feeding sensors, but no way to set the feeding schedule that the
feeder runs by itself. This quirk adds the schedule as entities: per slot an enabled
switch, a days select, hour, minute and portions. A change waits a few seconds so that
several edits go out together, then the whole schedule is written to the feeder in the
format Zigbee2MQTT uses. The feeder then feeds without Home Assistant.

The feeder takes 5 slots at most: a write of 6 is ignored as a whole (observed on the
hardware with Zigbee2MQTT), so the quirk offers 5.

The quirk keeps the upstream cluster (zhaquirks.xiaomi.aqara.feeder_acn001) and the
entities ZHA already creates for it, and adds to them. It also answers the feeder's
time poll with local time, because the feeder uses that as its wall clock and the
schedule has no timezone (same as Zigbee2MQTT, and as zigpy/zha-device-handlers#5294
and #5400 propose).
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any, Final

from zigpy import types
from zigpy.zcl import foundation
from zigpy.zcl.clusters.general import OnOff, Time
from zigpy.zcl.foundation import ZCLAttributeDef

from zhaquirks import CustomCluster
from zhaquirks.builder import EntityPlatform, EntityType, QuirkBuilder
from zhaquirks.xiaomi.aqara.feeder_acn001 import (
    FEEDER_ATTR,
    FEEDING_REPORT,
    SCHEDULING_STRING,
    ZCL_CHILD_LOCK,
    ZCL_DISABLE_LED_INDICATOR,
    ZCL_ERROR_DETECTED,
    ZCL_FEEDING,
    ZCL_FEEDING_MODE,
    ZCL_LAST_FEEDING_SIZE,
    ZCL_LAST_FEEDING_SOURCE,
    ZCL_PORTION_WEIGHT,
    ZCL_PORTIONS_DISPENSED,
    ZCL_SERVING_SIZE,
    ZCL_WEIGHT_DISPENSED,
    FeedingMode,
    OppleCluster as AqaraOppleCluster,
)

LOGGER = logging.getLogger(__name__)

MANUFACTURER_CODE: Final = 0x115F
MODEL: Final = "aqara.feeder.acn001"

# The feeder ignores a write of 6 slots (11 bytes per slot, the frame is too long)
SCHEDULE_SLOTS: Final = 5
# Seconds a change waits for further changes before the schedule is written
SCHEDULE_WRITE_DELAY: Final = 5.0

# Local attribute ids, never sent to the feeder (writes are turned into one
# schedule write). Five attributes per slot, 8 ids apart.
SLOT_ATTR_BASE: Final = 0x1400
SLOT_ATTR_STRIDE: Final = 8
ZCL_SCHEDULE_STATUS: Final = 0x14F0
ZCL_SEND_SCHEDULE: Final = 0x14F1

# Default time of each slot, spread over the day
SLOT_DEFAULT_HOURS: Final = (7, 11, 15, 19, 23)
SLOT_DEFAULT_PORTIONS: Final = 1


class FeedingDays(types.enum8):
    """Days of a slot, the value is the feeder's day bit mask (Monday is bit 0)."""

    Every_day = 0x7F
    Workdays = 0x1F
    Weekend = 0x60
    Monday = 0x01
    Tuesday = 0x02
    Wednesday = 0x04
    Thursday = 0x08
    Friday = 0x10
    Saturday = 0x20
    Sunday = 0x40
    Mon_Wed_Fri_Sun = 0x55
    Tue_Thu_Sat = 0x2A


class LastFeedingSource(types.enum8):
    """What started the last feeding (upstream's enum has no schedule)."""

    Schedule = 0x00
    Feeder = 0x01
    Remote = 0x02


class ScheduleStatus(types.enum8):
    """What happened to the schedule that was last written."""

    Unknown = 0x00
    Pending = 0x01
    Written = 0x02
    Confirmed = 0x03
    Failed = 0x04
    # The slot entities are placeholders: nothing was written or taken over since
    # the last start, so edits are not sent on their own
    Not_synced = 0x05


def slot_attr_id(slot: int, offset: int) -> int:
    """Return the local attribute id of a slot field (slot counts from 1)."""
    return SLOT_ATTR_BASE + (slot - 1) * SLOT_ATTR_STRIDE + offset


# Offsets within a slot
ENABLED: Final = 0
DAYS: Final = 1
HOUR: Final = 2
MINUTE: Final = 3
PORTIONS: Final = 4

SLOT_FIELD_TYPES: Final = {
    ENABLED: ("enabled", types.Bool),
    DAYS: ("days", FeedingDays),
    HOUR: ("hour", types.uint8_t),
    MINUTE: ("minute", types.uint8_t),
    PORTIONS: ("portions", types.uint8_t),
}

SLOT_FIELD_LIMITS: Final = {
    HOUR: (0, 23),
    MINUTE: (0, 59),
    PORTIONS: (1, 10),
}


def slot_attr_name(slot: int, offset: int) -> str:
    """Return the attribute name of a slot field."""
    return f"slot{slot}_{SLOT_FIELD_TYPES[offset][0]}"


def _attribute(attr_id: int, name: str, attr_type: type, access: str = "rwp"):
    return ZCLAttributeDef(
        id=attr_id,
        name=name,
        type=attr_type,
        access=access,
        is_manufacturer_specific=True,
        manufacturer_code=MANUFACTURER_CODE,
    )


_LOCAL_ATTRIBUTES: Final = [
    _attribute(slot_attr_id(slot, offset), slot_attr_name(slot, offset), field_type)
    for slot in range(1, SCHEDULE_SLOTS + 1)
    for offset, (_, field_type) in SLOT_FIELD_TYPES.items()
] + [
    _attribute(ZCL_SCHEDULE_STATUS, "schedule_status", ScheduleStatus),
    _attribute(ZCL_SEND_SCHEDULE, "send_schedule", types.Bool),
]

_LOCAL_ATTRIBUTE_IDS: Final = frozenset(attr.id for attr in _LOCAL_ATTRIBUTES)

# Attributes that only exist in the quirk: upstream's made-up ones (0x1388 to 0x1392,
# the feeder reports its state packed in 0xFFF1) and the slot ones above. The feeder
# answers UNSUPPORTED to a read of any of them, so they must never be asked for.
_VIRTUAL_ATTRIBUTE_IDS: Final = (
    frozenset(range(ZCL_FEEDING, ZCL_PORTION_WEIGHT + 1)) | _LOCAL_ATTRIBUTE_IDS
)

# The upstream attribute definitions plus the local ones
_FeederAttributeDefs = type(
    "AttributeDefs",
    (AqaraOppleCluster.AttributeDefs,),
    {
        # Same attribute as upstream's, with an enum that knows the schedule
        "last_feeding_source": ZCLAttributeDef(
            id=ZCL_LAST_FEEDING_SOURCE,
            type=LastFeedingSource,
            manufacturer_code=MANUFACTURER_CODE,
        ),
        **{attr.name: attr for attr in _LOCAL_ATTRIBUTES},
    },
)


def encode_schedule(slots: list[tuple[int, int, int, int]]) -> bytes:
    """Encode (days mask, hour, minute, portions) slots like Zigbee2MQTT does.

    Each slot is 5 bytes (mask, hour, minute, portions, 0) as lower case hex text,
    slots are joined by commas and the string ends with a NUL byte.
    """
    text = ",".join(
        bytes([mask, hour, minute, portions, 0]).hex()
        for mask, hour, minute, portions in slots
    )
    return text.encode("ascii") + b"\x00"


def decode_schedule(raw: bytes) -> list[tuple[int, int, int, int]] | None:
    """Decode the schedule string the feeder reports, None if it is not one."""
    text = raw.rstrip(b"\x00").decode("ascii", errors="ignore").strip()
    slots = []

    if not text:
        return slots

    for token in text.split(","):
        token = token.strip()

        # The feeder marks an unused slot with //
        if token == "//":
            continue

        try:
            data = bytes.fromhex(token)
        except ValueError:
            return None

        if len(data) != 5:
            return None

        slots.append((data[0], data[1], data[2], data[3]))

    return slots


class AqaraFeederCluster(AqaraOppleCluster):
    """Aqara manufacturer cluster of the feeder with the schedule slots."""

    AttributeDefs = _FeederAttributeDefs

    # A replaced cluster does not keep values set in __init__, so defaults are served
    # by get(). This also covers the defaults upstream seeds for its own attributes,
    # which ZHA's feeder entities need.
    _DEFAULT_VALUES: dict[int, Any] = {
        ZCL_DISABLE_LED_INDICATOR: False,
        ZCL_CHILD_LOCK: False,
        ZCL_FEEDING_MODE: FeedingMode.Manual,
        ZCL_SERVING_SIZE: 1,
        ZCL_PORTION_WEIGHT: 8,
        ZCL_ERROR_DETECTED: False,
        ZCL_PORTIONS_DISPENSED: 0,
        ZCL_WEIGHT_DISPENSED: 0,
        ZCL_SCHEDULE_STATUS: ScheduleStatus.Not_synced,
        ZCL_SEND_SCHEDULE: False,
        **{
            slot_attr_id(slot, offset): default
            for slot in range(1, SCHEDULE_SLOTS + 1)
            for offset, default in {
                ENABLED: False,
                DAYS: FeedingDays.Every_day,
                HOUR: SLOT_DEFAULT_HOURS[slot - 1],
                MINUTE: 0,
                PORTIONS: SLOT_DEFAULT_PORTIONS,
            }.items()
        },
    }

    # Class level because upstream's __init__ already emits attribute events
    _last_event: Any = None

    def __init__(self, *args, **kwargs) -> None:
        """Set up the schedule bookkeeping."""
        super().__init__(*args, **kwargs)
        self._send_task: asyncio.Task | None = None
        # The schedule that was written last, to confirm the feeder's report of it
        self._expected: list[tuple[int, int, int, int]] | None = None
        # The slot values are not saved across restarts, so after a start they are
        # placeholders and the feeder still holds the real schedule. They count as
        # known once the schedule was written or taken over from the feeder.
        self._slots_known = False
        self._slots_logged = False

    def get(self, key: int | str, default: Any | None = None) -> Any:
        """Get a cached attribute, falling back to the defaults."""
        try:
            attr_def = self.find_attribute(key)
        except KeyError:
            return super().get(key, default)

        value = super().get(key)

        if value is not None:
            return value

        return self._DEFAULT_VALUES.get(attr_def.id, default)

    async def read_attributes(self, attributes, *args, **kwargs):
        """Answer reads of the made-up attributes locally, never ask the feeder.

        ZHA reads the attributes of its feeder entities when a device starts up and
        asks the device for every one that is not in the cache. The feeder answers
        UNSUPPORTED, zigpy then stores that, and ZHA skips the entities for good.
        Upstream's quirk avoids the read by seeding the cache in __init__, which a
        replaced cluster loses, so the values are served from the cache or the
        defaults here.
        """
        self._log_restored_slots()

        local: dict[Any, Any] = {}
        remote = []

        for attribute in attributes:
            try:
                attr_id = self.find_attribute(attribute).id
            except KeyError:
                remote.append(attribute)
                continue

            if attr_id not in _VIRTUAL_ATTRIBUTE_IDS:
                remote.append(attribute)
                continue

            value = self.get(attr_id)

            if value is not None:
                local[attribute] = value

        if not remote:
            return local, {}

        success, failure = await super().read_attributes(remote, *args, **kwargs)
        success.update(local)

        return success, failure

    def _log_restored_slots(self) -> None:
        """Say once per start whether the slot values came back from the cache."""
        if self._slots_logged:
            return

        self._slots_logged = True
        restored = sum(
            super(AqaraFeederCluster, self).get(attr.id) is not None
            for attr in _LOCAL_ATTRIBUTES
            if attr.id < ZCL_SCHEDULE_STATUS
        )
        LOGGER.info(
            "Feeding schedule slots restored from the cache: %s of %s values",
            restored,
            SCHEDULE_SLOTS * len(SLOT_FIELD_TYPES),
        )

    def enabled_slots(self) -> list[tuple[int, int, int, int]]:
        """Return the enabled slots as (days mask, hour, minute, portions), by time."""
        slots = []

        for slot in range(1, SCHEDULE_SLOTS + 1):
            if not self.get(slot_attr_id(slot, ENABLED)):
                continue

            slots.append(
                (
                    int(self.get(slot_attr_id(slot, DAYS))),
                    int(self.get(slot_attr_id(slot, HOUR))),
                    int(self.get(slot_attr_id(slot, MINUTE))),
                    int(self.get(slot_attr_id(slot, PORTIONS))),
                )
            )

        return sorted(slots, key=lambda slot: (slot[1], slot[2], slot[0]))

    def _set_status(self, status: ScheduleStatus) -> None:
        self._update_attribute(ZCL_SCHEDULE_STATUS, status)

    def _cancel_pending_send(self) -> None:
        if self._send_task is not None and not self._send_task.done():
            self._send_task.cancel()

        self._send_task = None

    def _write_pending(self) -> bool:
        return self._send_task is not None and not self._send_task.done()

    def _schedule_send(self) -> None:
        """Write the schedule after a short delay, restarting it on every change."""
        self._cancel_pending_send()
        self._set_status(ScheduleStatus.Pending)
        self._send_task = asyncio.get_running_loop().create_task(self._send_later())

    async def _send_later(self) -> None:
        await asyncio.sleep(SCHEDULE_WRITE_DELAY)
        await self.send_schedule()

    @staticmethod
    def _all_success(result: Any) -> bool:
        """Check a write attributes response for plain success."""
        records = [
            record
            for part in (result if isinstance(result, (list, tuple)) else [result])
            for record in (part if isinstance(part, (list, tuple)) else [part])
        ]

        return bool(records) and all(
            getattr(record, "status", None) == foundation.Status.SUCCESS
            for record in records
        )

    async def send_schedule(self) -> bool:
        """Write the enabled slots to the feeder, True if the feeder accepted it."""
        # A write that is still waiting is replaced by this one
        if self._send_task is not asyncio.current_task():
            self._cancel_pending_send()

        slots = self.enabled_slots()

        # Placeholders with every slot off would wipe the feeder's real schedule
        if not self._slots_known and not slots:
            LOGGER.warning(
                "Not writing an empty feeding schedule before the slots are known, "
                "set the slots first"
            )
            self._set_status(ScheduleStatus.Not_synced)
            return False

        payload = encode_schedule(slots)
        self._expected = slots

        # An empty schedule is a single NUL byte, which the framing sends as a number
        if len(payload) == 1:
            name, cooked = self._build_feeder_attribute(SCHEDULING_STRING, 0, 1)
        else:
            name, cooked = self._build_feeder_attribute(
                SCHEDULING_STRING, payload, len(payload)
            )

        try:
            result = await AqaraOppleCluster.write_attributes(self, {name: cooked})
        except Exception as exc:  # noqa: BLE001
            LOGGER.warning("Writing the feeding schedule failed: %s", exc)
            self._expected = None
            self._set_status(ScheduleStatus.Failed)
            return False

        if not self._all_success(result):
            LOGGER.warning("The feeder refused the feeding schedule: %s", result)
            self._expected = None
            self._set_status(ScheduleStatus.Failed)
            return False

        self._slots_known = True

        # The feeder reports the schedule back, which can already have confirmed it
        if self.get(ZCL_SCHEDULE_STATUS) != ScheduleStatus.Confirmed:
            self._set_status(ScheduleStatus.Written)

        return True

    def _handle_schedule_report(self, raw: bytes) -> None:
        """Confirm a schedule the feeder reports, or take over one set elsewhere."""
        slots = decode_schedule(raw)

        if slots is None:
            LOGGER.debug("Unreadable feeding schedule report: %s", raw)
            return

        if self._expected is not None and slots == self._expected:
            self._slots_known = True
            self._set_status(ScheduleStatus.Confirmed)
            return

        # Do not overwrite the entities while they are being edited
        if self._write_pending():
            return

        if slots == self.enabled_slots():
            self._slots_known = True
            return

        LOGGER.debug("Taking over the feeding schedule from the feeder: %s", slots)
        known = {int(day) for day in FeedingDays}

        for slot in range(1, SCHEDULE_SLOTS + 1):
            index = slot - 1

            if index < len(slots) and slots[index][0] in known:
                mask, hour, minute, portions = slots[index]
                self._update_attribute(slot_attr_id(slot, ENABLED), True)
                self._update_attribute(slot_attr_id(slot, DAYS), FeedingDays(mask))
                self._update_attribute(slot_attr_id(slot, HOUR), hour)
                self._update_attribute(slot_attr_id(slot, MINUTE), minute)
                self._update_attribute(slot_attr_id(slot, PORTIONS), portions)
            else:
                self._update_attribute(slot_attr_id(slot, ENABLED), False)

        self._slots_known = True
        self._expected = None

    def _handle_attribute_event(self, event) -> None:
        """Handle an attribute event once, the upstream clusters register it twice."""
        # Only the packed feeder attribute is looked at, and parsing it updates other
        # attributes, so remember this event apart from the nested ones
        if event.attribute_id == FEEDER_ATTR:
            if event is self._last_event:
                return

            self._last_event = event

        super()._handle_attribute_event(event)

    def _parse_feeder_attribute(self, value: bytes) -> None:
        """Look at every frame of the packed feeder attribute, schedule included."""
        try:
            attribute, _ = types.int32s_be.deserialize(value[3:7])
            length = value[7]
            payload = bytes(value[8 : 8 + length])
        except (IndexError, ValueError, TypeError):
            # The feeder reports a 5 slot schedule as fragments that are too short
            LOGGER.debug("Skipping a feeder report that is not a full frame: %s", value)
            return

        LOGGER.info("Feeder report 0x%08X: %s", attribute, payload)

        if attribute == FEEDING_REPORT:
            self._handle_feeding_report(payload)
            return

        try:
            super()._parse_feeder_attribute(value)
        except (IndexError, ValueError, TypeError):
            LOGGER.warning("Could not read the feeder report %s", value, exc_info=True)

        if attribute == SCHEDULING_STRING:
            self._handle_schedule_report(payload)

    def _handle_feeding_report(self, payload: bytes) -> None:
        """Take what started the feeding and how many portions from its report."""
        try:
            text = payload.decode("utf-8")
            source = LastFeedingSource(int(text[0:2], 16))
            size = int(text[3:4], 16)
        except (UnicodeDecodeError, ValueError):
            LOGGER.warning("Could not read the feeding report %s", payload)
            return

        self._update_attribute(ZCL_LAST_FEEDING_SOURCE, source)
        self._update_attribute(ZCL_LAST_FEEDING_SIZE, size)

    def _check_slot_value(self, attr_id: int, value: Any) -> Any:
        offset = (attr_id - SLOT_ATTR_BASE) % SLOT_ATTR_STRIDE

        if offset == ENABLED:
            return bool(value)

        if offset == DAYS:
            return FeedingDays(value)

        low, high = SLOT_FIELD_LIMITS[offset]
        number = int(value)

        if not low <= number <= high:
            raise ValueError(
                f"{SLOT_FIELD_TYPES[offset][0]} must be {low} to {high}, got {value}"
            )

        return number

    async def write_attributes(self, attributes, **kwargs):
        """Take slot writes locally and send one schedule, pass the rest on."""
        local: dict[int, Any] = {}
        remote: dict[Any, Any] = {}

        for key, value in attributes.items():
            try:
                attr_id = self.find_attribute(key).id
            except KeyError:
                attr_id = None

            if attr_id in _LOCAL_ATTRIBUTE_IDS:
                local[attr_id] = value
            else:
                remote[key] = value

        if not local:
            return await super().write_attributes(attributes, **kwargs)

        if ZCL_SCHEDULE_STATUS in local:
            raise ValueError("The schedule status is read only")

        # Check everything first so that a bad value changes nothing
        checked = {
            attr_id: self._check_slot_value(attr_id, value)
            for attr_id, value in local.items()
            if attr_id not in (ZCL_SEND_SCHEDULE,)
        }

        result = [[foundation.WriteAttributesStatusRecord(foundation.Status.SUCCESS)]]

        if remote:
            result = await super().write_attributes(remote, **kwargs)

        for attr_id, value in checked.items():
            self._update_attribute(attr_id, value)

        if ZCL_SEND_SCHEDULE in local:
            # The send button, no waiting
            await self.send_schedule()
        elif checked:
            if self._slots_known:
                self._schedule_send()
            else:
                # The other slots may be placeholders, writing would wipe the feeder
                self._set_status(ScheduleStatus.Not_synced)

        return result


class FeederTimeCluster(CustomCluster, Time):
    """Time cluster that answers the feeder's time poll with local time.

    The feeder stores the answer as its own wall clock and runs the schedule from it.
    The schedule carries no timezone, so a spec compliant UTC answer would shift every
    feeding by the UTC offset. Zigbee2MQTT answers with local time too. The override
    stays out of zigpy's cluster registry, so no other device is affected.
    """

    _skip_registry = True

    def handle_read_attribute_time(self) -> types.UTCTime:
        """Return the current local time as seconds since 2000-01-01."""
        return types.UTCTime(int(self.handle_read_attribute_local_time()))


def _add_slot_entities(builder: QuirkBuilder) -> QuirkBuilder:
    cluster_id = AqaraFeederCluster.cluster_id

    for slot in range(1, SCHEDULE_SLOTS + 1):
        builder = (
            builder.switch(
                slot_attr_name(slot, ENABLED),
                cluster_id,
                unique_id_suffix=f"feeding_{slot}_enabled",
                translation_key=f"feeding_{slot}_enabled",
                fallback_name=f"Feeding {slot} enabled",
            )
            .enum(
                slot_attr_name(slot, DAYS),
                FeedingDays,
                cluster_id,
                unique_id_suffix=f"feeding_{slot}_days",
                translation_key=f"feeding_{slot}_days",
                fallback_name=f"Feeding {slot} days",
            )
            .number(
                slot_attr_name(slot, HOUR),
                cluster_id,
                min_value=SLOT_FIELD_LIMITS[HOUR][0],
                max_value=SLOT_FIELD_LIMITS[HOUR][1],
                step=1,
                mode="box",
                unique_id_suffix=f"feeding_{slot}_hour",
                translation_key=f"feeding_{slot}_hour",
                fallback_name=f"Feeding {slot} hour",
            )
            .number(
                slot_attr_name(slot, MINUTE),
                cluster_id,
                min_value=SLOT_FIELD_LIMITS[MINUTE][0],
                max_value=SLOT_FIELD_LIMITS[MINUTE][1],
                step=1,
                mode="box",
                unique_id_suffix=f"feeding_{slot}_minute",
                translation_key=f"feeding_{slot}_minute",
                fallback_name=f"Feeding {slot} minute",
            )
            .number(
                slot_attr_name(slot, PORTIONS),
                cluster_id,
                min_value=SLOT_FIELD_LIMITS[PORTIONS][0],
                max_value=SLOT_FIELD_LIMITS[PORTIONS][1],
                step=1,
                mode="box",
                unit="portions",
                unique_id_suffix=f"feeding_{slot}_portions",
                translation_key=f"feeding_{slot}_portions",
                fallback_name=f"Feeding {slot} portions",
            )
        )

    return builder


(
    _add_slot_entities(
        QuirkBuilder(None, MODEL)
        .friendly_name(manufacturer="Aqara", model=MODEL)
        .removes(OnOff.cluster_id)
        .adds(FeederTimeCluster)
        .replaces(AqaraFeederCluster)
        # ZHA's own source sensor has no schedule option, ours takes its place
        .prevent_default_entity_creation(
            endpoint_id=1,
            cluster_id=AqaraFeederCluster.cluster_id,
            function=lambda entity: (
                entity.__class__.__name__ == "AqaraPetFeederLastFeedingSource"
            ),
        )
    )
    .enum(
        "last_feeding_source",
        LastFeedingSource,
        AqaraFeederCluster.cluster_id,
        entity_platform=EntityPlatform.SENSOR,
        entity_type=EntityType.DIAGNOSTIC,
        unique_id_suffix="last_feeding_source",
        translation_key="last_feeding_source_c1",
        fallback_name="Last feeding source",
    )
    .enum(
        "schedule_status",
        ScheduleStatus,
        AqaraFeederCluster.cluster_id,
        entity_platform=EntityPlatform.SENSOR,
        entity_type=EntityType.DIAGNOSTIC,
        unique_id_suffix="schedule_status",
        translation_key="schedule_status",
        fallback_name="Schedule status",
    )
    .write_attr_button(
        "send_schedule",
        1,
        AqaraFeederCluster.cluster_id,
        entity_type=EntityType.CONFIG,
        unique_id_suffix="send_schedule",
        translation_key="send_schedule",
        fallback_name="Send schedule",
    )
    .add_to_registry()
)
