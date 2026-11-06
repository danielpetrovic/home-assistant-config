"""Quirk V2 for Aqara aqara.feeder.acn001 with an on-device feeding schedule.

On top of the upstream quirk this adds the schedule the feeder runs by itself: per slot
an enabled switch, a days select, hour, minute and portions. A change waits a few
seconds so that several edits go out together, then the whole schedule is written to
the feeder in the format Zigbee2MQTT uses. The feeder takes 5 slots at most: a write of
6 is ignored as a whole. The feeder also gets its wall clock from the time poll, so the
Time cluster answers with local time, because the schedule carries no timezone.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any, Final

from zigpy import types
from zigpy.time import TimeSnapshot
from zigpy.typing import UNDEFINED, UndefinedType
from zigpy.zcl import (
    AttributeReadEvent,
    AttributeReportedEvent,
    AttributeUpdatedEvent,
    foundation,
)
from zigpy.zcl.clusters.general import OnOff, Time
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

from zhaquirks.builder import (
    BinarySensorDeviceClass,
    EntityPlatform,
    EntityType,
    QuirkBuilder,
    SensorStateClass,
    UnitOfMass,
)
from zhaquirks.clusters import CustomCluster
from zhaquirks.xiaomi import XiaomiAqaraE1Cluster

# 32 bit signed integer values that are encoded in FEEDER_ATTR = 0xFFF1
FEEDING = 0x04150055
FEEDING_REPORT = 0x041502BC
PORTIONS_DISPENSED = 0x0D680055
WEIGHT_DISPENSED = 0x0D690055
ERROR_DETECTED = 0x0D0B0055
SCHEDULING_STRING = 0x080008C8
DISABLE_LED_INDICATOR = 0x04170055
CHILD_LOCK = 0x04160055
FEEDING_MODE = 0x04180055
SERVING_SIZE = 0x0E5C0055
PORTION_WEIGHT = 0x0E5F0055

FEEDER_ATTR = 0xFFF1
FEEDER_ATTR_NAME = "feeder_attr"

# Fake ZCL attribute ids we can use for entities for the opple cluster
ZCL_FEEDING = 0x1388
ZCL_LAST_FEEDING_SOURCE = 0x1389
ZCL_LAST_FEEDING_SIZE = 0x138A
ZCL_PORTIONS_DISPENSED = 0x138B
ZCL_WEIGHT_DISPENSED = 0x138C
ZCL_ERROR_DETECTED = 0x138D
ZCL_DISABLE_LED_INDICATOR = 0x138E
ZCL_CHILD_LOCK = 0x138F
ZCL_FEEDING_MODE = 0x1390
ZCL_SERVING_SIZE = 0x1391
ZCL_PORTION_WEIGHT = 0x1392

MANUFACTURER_CODE: Final = 0x115F

# The feeder ignores a write of 6 slots (11 bytes per slot, the frame is too long)
SCHEDULE_SLOTS: Final = 5
# Seconds a change waits for further changes before the schedule is written
SCHEDULE_WRITE_DELAY: Final = 5.0

# Quirk-only attribute ids for the schedule entities, never sent to the feeder (writes
# are turned into one schedule write). Five attributes per slot, 8 ids apart.
SLOT_ATTR_BASE: Final = 0x1400
SLOT_ATTR_STRIDE: Final = 8
ZCL_SCHEDULE_STATUS: Final = 0x14F0
ZCL_SEND_SCHEDULE: Final = 0x14F1

# Offsets within a slot
ENABLED: Final = 0
DAYS: Final = 1
HOUR: Final = 2
MINUTE: Final = 3
PORTIONS: Final = 4

LOCAL_ATTRIBUTE_IDS: Final = frozenset(
    {
        SLOT_ATTR_BASE + (slot - 1) * SLOT_ATTR_STRIDE + offset
        for slot in range(1, SCHEDULE_SLOTS + 1)
        for offset in range(PORTIONS + 1)
    }
    | {ZCL_SCHEDULE_STATUS, ZCL_SEND_SCHEDULE}
)

# Ids that only exist in the quirk, the feeder answers UNSUPPORTED to a read of them
FAKE_ATTRIBUTE_IDS: Final = (
    frozenset(range(ZCL_FEEDING, ZCL_PORTION_WEIGHT + 1)) | LOCAL_ATTRIBUTE_IDS
)

AQARA_TO_ZCL: dict[int, int] = {
    FEEDING: ZCL_FEEDING,
    ERROR_DETECTED: ZCL_ERROR_DETECTED,
    DISABLE_LED_INDICATOR: ZCL_DISABLE_LED_INDICATOR,
    CHILD_LOCK: ZCL_CHILD_LOCK,
    FEEDING_MODE: ZCL_FEEDING_MODE,
    SERVING_SIZE: ZCL_SERVING_SIZE,
    PORTION_WEIGHT: ZCL_PORTION_WEIGHT,
}

ZCL_TO_AQARA: dict[int, int] = {
    ZCL_FEEDING: FEEDING,
    ZCL_DISABLE_LED_INDICATOR: DISABLE_LED_INDICATOR,
    ZCL_CHILD_LOCK: CHILD_LOCK,
    ZCL_FEEDING_MODE: FEEDING_MODE,
    ZCL_SERVING_SIZE: SERVING_SIZE,
    ZCL_PORTION_WEIGHT: PORTION_WEIGHT,
    ZCL_ERROR_DETECTED: ERROR_DETECTED,
}

LOGGER = logging.getLogger(__name__)


class FeedingSource(types.enum8):
    """Feeding source."""

    Schedule = 0x00
    Feeder = 0x01
    HomeAssistant = 0x02


class FeedingMode(types.enum8):
    """Feeding mode."""

    Manual = 0x00
    Schedule = 0x01


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


# Default time of each slot, spread over the day
SLOT_DEFAULT_HOURS: Final = (7, 11, 15, 19, 23)
SLOT_DEFAULT_PORTIONS: Final = 1

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


LOCAL_ATTRIBUTES: Final = [
    _attribute(slot_attr_id(slot, offset), slot_attr_name(slot, offset), field_type)
    for slot in range(1, SCHEDULE_SLOTS + 1)
    for offset, (_, field_type) in SLOT_FIELD_TYPES.items()
] + [
    _attribute(ZCL_SCHEDULE_STATUS, "schedule_status", ScheduleStatus),
    _attribute(ZCL_SEND_SCHEDULE, "send_schedule", types.Bool),
]

SLOT_DEFAULTS: Final = {
    slot_attr_id(slot, offset): default
    for slot in range(1, SCHEDULE_SLOTS + 1)
    for offset, default in {
        ENABLED: False,
        DAYS: FeedingDays.Every_day,
        HOUR: SLOT_DEFAULT_HOURS[slot - 1],
        MINUTE: 0,
        PORTIONS: SLOT_DEFAULT_PORTIONS,
    }.items()
}


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


class OppleCluster(XiaomiAqaraE1Cluster):
    """Opple cluster."""

    class AttributeDefs(BaseAttributeDefs):
        """Attribute definitions."""

        feeding: Final = ZCLAttributeDef(
            id=ZCL_FEEDING, type=types.Bool, manufacturer_code=0x115F
        )
        last_feeding_source: Final = ZCLAttributeDef(
            id=ZCL_LAST_FEEDING_SOURCE, type=FeedingSource, manufacturer_code=0x115F
        )
        last_feeding_size: Final = ZCLAttributeDef(
            id=ZCL_LAST_FEEDING_SIZE, type=types.uint8_t, manufacturer_code=0x115F
        )
        portions_dispensed: Final = ZCLAttributeDef(
            id=ZCL_PORTIONS_DISPENSED, type=types.uint16_t, manufacturer_code=0x115F
        )
        weight_dispensed: Final = ZCLAttributeDef(
            id=ZCL_WEIGHT_DISPENSED, type=types.uint32_t, manufacturer_code=0x115F
        )
        error_detected: Final = ZCLAttributeDef(
            id=ZCL_ERROR_DETECTED, type=types.Bool, manufacturer_code=0x115F
        )
        disable_led_indicator: Final = ZCLAttributeDef(
            id=ZCL_DISABLE_LED_INDICATOR, type=types.Bool, manufacturer_code=0x115F
        )
        child_lock: Final = ZCLAttributeDef(
            id=ZCL_CHILD_LOCK, type=types.Bool, manufacturer_code=0x115F
        )
        feeding_mode: Final = ZCLAttributeDef(
            id=ZCL_FEEDING_MODE, type=FeedingMode, manufacturer_code=0x115F
        )
        serving_size: Final = ZCLAttributeDef(
            id=ZCL_SERVING_SIZE, type=types.uint8_t, manufacturer_code=0x115F
        )
        portion_weight: Final = ZCLAttributeDef(
            id=ZCL_PORTION_WEIGHT, type=types.uint8_t, manufacturer_code=0x115F
        )
        feeder_attr: Final = ZCLAttributeDef(
            id=FEEDER_ATTR, type=types.LVBytes, manufacturer_code=0x115F
        )

    # The upstream definitions plus the quirk-only schedule attributes
    AttributeDefs = type(
        "AttributeDefs",
        (AttributeDefs,),
        {attr.name: attr for attr in LOCAL_ATTRIBUTES},
    )

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
        **SLOT_DEFAULTS,
    }

    def get(self, key: int | str, default: Any | None = None) -> Any:
        """Get cached attribute, falling back to feeder defaults."""
        try:
            attr_def = self.find_attribute(key)
        except KeyError:
            return super().get(key, default)

        value = super().get(key)
        if value is not None:
            return value
        return self._DEFAULT_VALUES.get(attr_def.id, default)

    async def read_attributes(
        self,
        attributes: list[int | str | foundation.ZCLAttributeDef],
        allow_cache: bool = False,
        only_cache: bool = False,
        manufacturer: int | UndefinedType | None = UNDEFINED,
        **kwargs,
    ) -> Any:
        """Read attributes, answering the quirk-defined ones without sending them.

        The device does not know the fake attribute ids and answers
        UNSUPPORTED_ATTRIBUTE, which would be persisted and make ZHA drop the
        entities. They are answered from the cache or `_DEFAULT_VALUES` instead,
        and those without either are left out of the result, so no mark is recorded.
        """
        self._log_restored_slots()

        success: dict[Any, Any] = {}
        attrs_to_read: list[int | str | foundation.ZCLAttributeDef] = []
        fake_defs: set[foundation.ZCLAttributeDef] = set()

        for attribute in attributes:
            try:
                attr_def = self.find_attribute(
                    attribute, manufacturer_code=manufacturer
                )
            except KeyError:
                attrs_to_read.append(attribute)
                continue

            if attr_def.id not in FAKE_ATTRIBUTE_IDS:
                attrs_to_read.append(attribute)
                continue

            if attr_def in fake_defs:
                raise ValueError(
                    f"Cannot read the same attribute twice in the same call: {attr_def}"
                )

            fake_defs.add(attr_def)
            value = super().get(attr_def.id)
            if value is None:
                value = self._DEFAULT_VALUES.get(attr_def.id)
            if value is not None:
                success[attribute] = attr_def.type(value)

        failure: dict[Any, Any] = {}
        if attrs_to_read:
            read_success, failure = await super().read_attributes(
                attrs_to_read,
                allow_cache=allow_cache,
                only_cache=only_cache,
                manufacturer=manufacturer,
                **kwargs,
            )
            success.update(read_success)

        return success, failure

    def is_attribute_unsupported(
        self, attr: int | str | foundation.ZCLAttributeDef
    ) -> bool:
        """Report the quirk-defined attributes as supported.

        This also masks UNSUPPORTED marks recorded by earlier versions of the quirk.
        """
        try:
            if self.find_attribute(attr).id in FAKE_ATTRIBUTE_IDS:
                return False
        except KeyError:
            pass
        return super().is_attribute_unsupported(attr)

    def __init__(self, *args, **kwargs):
        """Init."""
        super().__init__(*args, **kwargs)
        self._send_sequence: int = None
        self._send_task: asyncio.Task | None = None
        # The schedule that was written last, to confirm the feeder's report of it
        self._expected: list[tuple[int, int, int, int]] | None = None
        # The slot values are placeholders after a start while the feeder still holds
        # the real schedule. They count as known once the schedule was written or
        # taken over from the feeder.
        self._slots_known = False
        self._slots_logged = False

    def _handle_attribute_event(
        self, event: AttributeReportedEvent | AttributeUpdatedEvent | AttributeReadEvent
    ) -> None:
        """Handle attribute events: Xiaomi parsing first, then the feeder attribute."""
        super()._handle_attribute_event(event)

        if event.attribute_id == FEEDER_ATTR:
            self._parse_feeder_attribute(event.value)

    def _update_feeder_attribute(self, attrid: int, value: Any) -> None:
        zcl_attr_def = self.attributes.get(AQARA_TO_ZCL[attrid])
        self._update_attribute(zcl_attr_def.id, zcl_attr_def.type.deserialize(value)[0])

    def _parse_feeder_attribute(self, value: bytes) -> None:
        """Parse the feeder attribute."""
        # The feeder reports a 5 slot schedule in fragments that are too short
        if len(value) < 8:
            LOGGER.debug("Skipping a feeder report that is not a full frame: %s", value)
            return

        attribute, _ = types.int32s_be.deserialize(value[3:7])
        LOGGER.debug("OppleCluster._parse_feeder_attribute: attribute: %s", attribute)
        length, _ = types.uint8_t.deserialize(value[7:8])
        LOGGER.debug("OppleCluster._parse_feeder_attribute: length: %s", length)
        attribute_value = value[8 : (length + 8)]
        LOGGER.debug("OppleCluster._parse_feeder_attribute: value: %s", attribute_value)

        if attribute in AQARA_TO_ZCL:
            self._update_feeder_attribute(attribute, attribute_value)
        elif attribute == FEEDING_REPORT:
            attr_str = attribute_value.decode("utf-8")
            feeding_source = attr_str[0:2]
            feeding_size = attr_str[3:4]
            self._update_attribute(
                ZCL_LAST_FEEDING_SOURCE, FeedingSource(feeding_source)
            )
            self._update_attribute(ZCL_LAST_FEEDING_SIZE, int(feeding_size, base=16))
        elif attribute == PORTIONS_DISPENSED:
            portions_per_day, _ = types.uint16_t_be.deserialize(attribute_value)
            self._update_attribute(ZCL_PORTIONS_DISPENSED, portions_per_day)
        elif attribute == WEIGHT_DISPENSED:
            weight_per_day, _ = types.uint32_t_be.deserialize(attribute_value)
            self._update_attribute(ZCL_WEIGHT_DISPENSED, weight_per_day)
        elif attribute == SCHEDULING_STRING:
            self._handle_schedule_report(bytes(attribute_value))
        else:
            LOGGER.debug(
                "OppleCluster._parse_feeder_attribute: unhandled attribute: %s value: %s",
                attribute,
                attribute_value,
            )

    def _log_restored_slots(self) -> None:
        """Say once per start whether the slot values came back from the cache."""
        if self._slots_logged:
            return

        self._slots_logged = True
        cached = super().get
        restored = sum(
            cached(attr.id) is not None
            for attr in LOCAL_ATTRIBUTES
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
            result = await self.write_attributes({name: cooked})
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

    def _is_local_attribute(self, key: int | str | foundation.ZCLAttributeDef) -> bool:
        try:
            return self.find_attribute(key).id in LOCAL_ATTRIBUTE_IDS
        except KeyError:
            return False

    async def _write_local_attributes(self, attributes: dict[Any, Any]) -> None:
        """Store slot writes and send one schedule."""
        local = {
            self.find_attribute(key).id: value for key, value in attributes.items()
        }

        if ZCL_SCHEDULE_STATUS in local:
            raise ValueError("The schedule status is read only")

        # Check everything first so that a bad value changes nothing
        checked = {
            attr_id: self._check_slot_value(attr_id, value)
            for attr_id, value in local.items()
            if attr_id != ZCL_SEND_SCHEDULE
        }

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

    def _build_feeder_attribute(
        self, attribute_id: int, value: Any = None, length: int | None = None
    ):
        """Build the Xiaomi feeder attribute."""
        LOGGER.debug(
            "OppleCluster.build_feeder_attribute: id: %s, value: %s length: %s",
            attribute_id,
            value,
            length,
        )
        self._send_sequence = ((self._send_sequence or 0) + 1) % 256
        val = bytes([0x00, 0x02, self._send_sequence])
        self._send_sequence += 1
        val += types.int32s_be(attribute_id).serialize()
        if length is not None and value is not None:
            val += types.uint8_t(length).serialize()
        if value is not None:
            if length == 1:
                val += types.uint8_t(value).serialize()
            elif length == 2:
                val += types.uint16_t_be(value).serialize()
            elif length == 4:
                val += types.uint32_t_be(value).serialize()
            else:
                val += value
        LOGGER.debug(
            "OppleCluster.build_feeder_attribute: id: %s, cooked value: %s length: %s",
            attribute_id,
            val,
            length,
        )
        return FEEDER_ATTR_NAME, val

    async def write_attributes(
        self,
        attributes: dict[str | int | foundation.ZCLAttributeDef, Any],
        **kwargs,
    ) -> list[list[foundation.WriteAttributesStatusRecord]]:
        """Write attributes to device with internal 'attributes' validation."""
        local = {
            key: value
            for key, value in attributes.items()
            if self._is_local_attribute(key)
        }
        if local:
            remote = {k: v for k, v in attributes.items() if k not in local}
            result = [
                [foundation.WriteAttributesStatusRecord(foundation.Status.SUCCESS)]
            ]
            if remote:
                result = await self.write_attributes(remote, **kwargs)
            await self._write_local_attributes(local)
            return result

        attrs = {}
        for attr, value in attributes.items():
            attr_def = self.find_attribute(attr)
            attr_id = attr_def.id
            if attr_id in ZCL_TO_AQARA:
                attribute, cooked_value = self._build_feeder_attribute(
                    ZCL_TO_AQARA[attr_id],
                    value,
                    4 if attr_def.name in ["serving_size", "portion_weight"] else 1,
                )
                attrs[attribute] = cooked_value
            else:
                attrs[attr] = value
        LOGGER.debug("OppleCluster.write_attributes: %s", attrs)
        # Skip attr cache because of the encoding from Xiaomi and
        # the attributes are reported back by the device
        kwargs.pop("update_cache", None)  # To not break when this is passed already
        return await super().write_attributes(attrs, update_cache=False, **kwargs)


class FeederTimeCluster(CustomCluster, Time):
    """Time cluster that answers the feeder's time poll with local time.

    The feeder stores the answer as its own wall clock and runs the schedule from it.
    The schedule carries no timezone, so a spec compliant UTC answer would shift every
    feeding by the UTC offset. Zigbee2MQTT answers with local time too. The override
    stays out of zigpy's cluster registry, so no other device is affected.
    """

    _skip_registry = True

    def handle_read_attribute_time(self, snapshot: TimeSnapshot) -> types.UTCTime:
        """Return the current local time as seconds since 2000-01-01."""
        return types.UTCTime(int(snapshot.local_time))


def _add_slot_entities(builder: QuirkBuilder) -> QuirkBuilder:
    cluster_id = OppleCluster.cluster_id

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


feeder_quirk = (
    # The device does not provide a manufacturer during the interview
    QuirkBuilder(None, "aqara.feeder.acn001")
    # v2 entities only take over cleanly once the native classes are removed
    # (zigpy/zha#883), if the model string matches
    .friendly_name(manufacturer="Aqara", model="aqara.feeder.acn001")
    .removes(OnOff.cluster_id)
    .adds(FeederTimeCluster)
    .replaces(OppleCluster)
    .enum(
        attribute_name=OppleCluster.AttributeDefs.last_feeding_source.name,
        enum_class=FeedingSource,
        cluster_id=OppleCluster.cluster_id,
        entity_platform=EntityPlatform.SENSOR,
        entity_type=EntityType.STANDARD,
        unique_id_suffix=f"{OppleCluster.cluster_id}-last_feeding_source",
        translation_key="last_feeding_source",
        fallback_name="Last feeding source",
    )
    .sensor(
        attribute_name=OppleCluster.AttributeDefs.last_feeding_size.name,
        cluster_id=OppleCluster.cluster_id,
        unique_id_suffix=f"{OppleCluster.cluster_id}-last_feeding_size",
        translation_key="last_feeding_size",
        fallback_name="Last feeding size",
    )
    .sensor(
        attribute_name=OppleCluster.AttributeDefs.portions_dispensed.name,
        cluster_id=OppleCluster.cluster_id,
        state_class=SensorStateClass.TOTAL_INCREASING,
        unique_id_suffix=f"{OppleCluster.cluster_id}-portions_dispensed",
        translation_key="portions_dispensed_today",
        fallback_name="Portions dispensed today",
    )
    .sensor(
        attribute_name=OppleCluster.AttributeDefs.weight_dispensed.name,
        cluster_id=OppleCluster.cluster_id,
        unit=UnitOfMass.GRAMS,
        state_class=SensorStateClass.TOTAL_INCREASING,
        unique_id_suffix=f"{OppleCluster.cluster_id}-weight_dispensed",
        translation_key="weight_dispensed_today",
        fallback_name="Weight dispensed today",
    )
    .switch(
        attribute_name=OppleCluster.AttributeDefs.disable_led_indicator.name,
        cluster_id=OppleCluster.cluster_id,
        force_inverted=True,
        unique_id_suffix=f"{OppleCluster.cluster_id}-disable_led_indicator",
        translation_key="led_indicator",
        fallback_name="LED indicator",
    )
    .switch(
        attribute_name=OppleCluster.AttributeDefs.child_lock.name,
        cluster_id=OppleCluster.cluster_id,
        unique_id_suffix=f"{OppleCluster.cluster_id}-child_lock",
        translation_key="child_lock",
        fallback_name="Child lock",
    )
    .enum(
        attribute_name=OppleCluster.AttributeDefs.feeding_mode.name,
        enum_class=FeedingMode,
        cluster_id=OppleCluster.cluster_id,
        unique_id_suffix=f"{OppleCluster.cluster_id}-feeding_mode",
        translation_key="feeding_mode",
        fallback_name="Feeding mode",
    )
    .number(
        attribute_name=OppleCluster.AttributeDefs.serving_size.name,
        cluster_id=OppleCluster.cluster_id,
        min_value=1,
        max_value=10,
        mode="box",
        unique_id_suffix=f"{OppleCluster.cluster_id}-serving_size",
        translation_key="serving_size",
        fallback_name="Serving size",
    )
    .number(
        attribute_name=OppleCluster.AttributeDefs.portion_weight.name,
        cluster_id=OppleCluster.cluster_id,
        min_value=1,
        max_value=100,
        unit=UnitOfMass.GRAMS,
        mode="box",
        unique_id_suffix=f"{OppleCluster.cluster_id}-portion_weight",
        translation_key="portion_weight",
        fallback_name="Portion weight",
    )
    .binary_sensor(
        attribute_name=OppleCluster.AttributeDefs.error_detected.name,
        cluster_id=OppleCluster.cluster_id,
        entity_type=EntityType.STANDARD,
        device_class=BinarySensorDeviceClass.PROBLEM,
        unique_id_suffix=f"{OppleCluster.cluster_id}-error_detected",
        fallback_name="Error detected",
    )
    .write_attr_button(
        attribute_name=OppleCluster.AttributeDefs.feeding.name,
        attribute_value=1,
        cluster_id=OppleCluster.cluster_id,
        entity_type=EntityType.STANDARD,
        unique_id_suffix=f"{OppleCluster.cluster_id}-feeding",
        translation_key="feed",
        fallback_name="Feed",
    )
    .enum(
        attribute_name=OppleCluster.AttributeDefs.schedule_status.name,
        enum_class=ScheduleStatus,
        cluster_id=OppleCluster.cluster_id,
        entity_platform=EntityPlatform.SENSOR,
        entity_type=EntityType.DIAGNOSTIC,
        unique_id_suffix="schedule_status",
        translation_key="schedule_status",
        fallback_name="Schedule status",
    )
    .write_attr_button(
        attribute_name=OppleCluster.AttributeDefs.send_schedule.name,
        attribute_value=1,
        cluster_id=OppleCluster.cluster_id,
        entity_type=EntityType.CONFIG,
        unique_id_suffix="send_schedule",
        translation_key="send_schedule",
        fallback_name="Send schedule",
    )
)

_add_slot_entities(feeder_quirk).add_to_registry()
