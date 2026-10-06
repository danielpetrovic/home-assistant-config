"""Shelly Ecowitt WS90 weather station: wind, UV and rain, plus calculated values.

Based on the upstream zhaquirks/shelly/ecowitt_ws90.py (same clusters, attribute names
and entity names, so the entity unique IDs stay the same), extended with:

- sensors calculated from the measurements (dew point, humidex, wind chill, apparent
  temperature, heat stress, rain rate, pressure trend and weather condition), ported
  from the Zigbee2MQTT converter so both stacks give the same values
- non-value markers and the all-zero frames the station sometimes sends are dropped
  before they reach the cache and the calculations
- battery voltage and the solar capacitor voltage (battery 2 voltage)
"""

from __future__ import annotations

import enum
import math
import time
from typing import Any

from zigpy import types as t
from zigpy.zcl.clusters.general import PowerConfiguration
from zigpy.zcl.clusters.measurement import (
    IlluminanceMeasurement,
    PressureMeasurement,
    RelativeHumidity,
    TemperatureMeasurement,
)
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

from zhaquirks import LocalDataCluster
from zhaquirks.builder import (
    DEGREE,
    PERCENTAGE,
    BinarySensorDeviceClass,
    EntityPlatform,
    EntityType,
    QuirkBuilder,
    ReportingConfig,
    SensorDeviceClass,
    SensorStateClass,
    UnitOfElectricPotential,
    UnitOfPrecipitationDepth,
    UnitOfSpeed,
    UnitOfTemperature,
    UnitOfVolumetricFlux,
)
from zhaquirks.clusters import CustomCluster
from zhaquirks.shelly import SHELLY_MANUFACTURER_CODE

# ZCL non-value markers (all bits set) the station reports when a reading is unavailable
NON_VALUE_UINT8 = 0xFF
NON_VALUE_UINT16 = 0xFFFF
NON_VALUE_UINT24 = 0xFFFFFF

# Calculated attributes are stored as integers in tenths
CALCULATED_SCALE = 10

# Rain rate is capped like in Zigbee2MQTT (mm/h)
RAIN_RATE_MAX = 300
# A rain rate needs at least a minute between two precipitation samples
RAIN_RATE_MIN_INTERVAL = 60
# A pressure trend needs at least 30 minutes between two pressure samples
PRESSURE_TREND_MIN_INTERVAL = 30 * 60


# Weather conditions, named like the Home Assistant weather entity conditions. The
# value is what is stored in the attribute, the name is the state of the enum sensor
WeatherCondition = enum.Enum(
    "WeatherCondition",
    {
        "sunny": 0,
        "clear-night": 1,
        "partlycloudy": 2,
        "cloudy": 3,
        "rainy": 4,
        "pouring": 5,
        "snowy": 6,
        "hail": 7,
        "windy": 8,
        "windy-variant": 9,
    },
)


def _round1(value: float) -> float:
    """Round to one decimal, halves up like Math.round in Zigbee2MQTT."""
    return math.floor(value * 10 + 0.5) / 10


def illuminance_lux(measured_value: int) -> int:
    """Convert the ZCL illuminance measured value to lux."""
    if measured_value <= 0:
        return 0
    return round(10 ** ((measured_value - 1) / 10000))


def calculate_dew_point(temp: float, humidity: float) -> float | None:
    """Dew point in °C (Magnus formula)."""
    if humidity <= 0:
        return None
    a = 17.27
    b = 237.7
    alpha = (a * temp) / (b + temp) + math.log(humidity / 100)
    return _round1((b * alpha) / (a - alpha))


def calculate_humidex(temp: float, humidity: float) -> float | None:
    """Humidex in °C (feels-like for warm and humid conditions)."""
    dew_point = calculate_dew_point(temp, humidity)
    if dew_point is None:
        return None
    ee = 6.11 * math.exp(5417.753 * (1 / 273.15 - 1 / (273.15 + dew_point)))
    return _round1(temp + 0.5555 * (ee - 10))


def calculate_wind_chill(temp: float, wind_ms: float) -> float:
    """Wind chill in °C, only below 10 °C and above 4.8 km/h wind, else the temperature."""
    wind_kmh = wind_ms * 3.6
    if temp > 10 or wind_kmh < 4.8:
        return _round1(temp)
    return _round1(
        13.12 + 0.6215 * temp - 11.37 * wind_kmh**0.16 + 0.3965 * temp * wind_kmh**0.16
    )


def calculate_apparent_temperature(
    temp: float, humidity: float | None, wind_ms: float | None
) -> float:
    """Apparent temperature in °C: wind chill when colder, humidex when warmer."""
    wind_chill = calculate_wind_chill(temp, wind_ms) if wind_ms is not None else None
    humidex = calculate_humidex(temp, humidity) if humidity is not None else None
    if wind_chill is not None and wind_chill < temp:
        return wind_chill
    if humidex is not None and humidex > temp:
        return humidex
    return _round1(temp)


def calculate_heat_stress(
    temp: float,
    humidity: float | None,
    lux: float | None,
    wind_ms: float | None,
    raining: bool,
) -> int:
    """Heat stress in percent (0 to 100), a sigmoid over temperature, humidity, sun, wind and rain.

    Rain takes 3 degrees off while the station reports it is raining. (Zigbee2MQTT
    passes the cumulative precipitation counter here, which made it always apply.)
    """
    solar = (lux or 0) / 100
    base = temp + solar / 100 + (humidity or 0) / 10
    cooled = base - (wind_ms or 0) / 2
    adjusted = cooled - (3 if raining else 0)
    scaled = (adjusted - 18) / (42 - 18)
    sigmoid = 1 / (1 + math.e ** (-4 * (scaled - 0.5)))
    return max(math.floor(sigmoid * 100 + 0.5), 0)


def calculate_weather_condition(
    *,
    temp: float | None,
    lux: float,
    raining: bool,
    wind_ms: float | None,
    rain_rate: float | None,
    pressure: float | None,
    pressure_trend: float | None,
) -> enum.Enum:
    """Weather condition from the measurements (same rules as Zigbee2MQTT)."""
    is_raining = raining and rain_rate is not None and rain_rate > 0
    is_pouring = is_raining and rain_rate > 10
    is_windy = wind_ms is not None and wind_ms > 10
    is_night = lux < 10

    is_low_pressure = pressure is not None and pressure < 1000
    is_pressure_falling = pressure_trend is not None and pressure_trend < -2

    is_hail = (
        is_raining
        and rain_rate > 5
        and lux < 5000
        and wind_ms is not None
        and wind_ms > 5
        and (is_low_pressure or is_pressure_falling)
    )
    is_snowing = is_raining and temp is not None and temp < 1 and not is_hail

    if is_hail:
        return WeatherCondition["hail"]
    if is_snowing:
        return WeatherCondition["snowy"]
    if is_pouring:
        return WeatherCondition["pouring"]
    if is_raining:
        return WeatherCondition["rainy"]

    if is_night:
        return (
            WeatherCondition["windy"] if is_windy else WeatherCondition["clear-night"]
        )

    if lux > 40000:
        return WeatherCondition["windy"] if is_windy else WeatherCondition["sunny"]
    if lux > 10000:
        return (
            WeatherCondition["windy-variant"]
            if is_windy
            else WeatherCondition["partlycloudy"]
        )
    return WeatherCondition["cloudy"]


class ShellyWS90CalculatedCluster(LocalDataCluster):
    """Local cluster holding the values calculated from the station measurements."""

    cluster_id = 0xFC80
    name = "Shelly WS90 Calculated Cluster"
    ep_attribute = "shelly_ws90_calculated"

    class AttributeDefs(BaseAttributeDefs):
        """Calculated attribute definitions, temperatures and rates in tenths."""

        dew_point = ZCLAttributeDef(id=0x0000, type=t.int16s, access="rp")
        humidex = ZCLAttributeDef(id=0x0001, type=t.int16s, access="rp")
        wind_chill = ZCLAttributeDef(id=0x0002, type=t.int16s, access="rp")
        apparent_temperature = ZCLAttributeDef(id=0x0003, type=t.int16s, access="rp")
        heat_stress = ZCLAttributeDef(id=0x0004, type=t.uint8_t, access="rp")
        rain_rate = ZCLAttributeDef(id=0x0005, type=t.uint16_t, access="rp")
        pressure_trend = ZCLAttributeDef(id=0x0006, type=t.int16s, access="rp")
        weather_condition = ZCLAttributeDef(id=0x0007, type=t.uint8_t, access="rp")

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        """Init, the sampling histories only live as long as the cluster."""
        super().__init__(*args, **kwargs)
        self._precipitation_history: tuple[float, float] | None = None
        self._pressure_history: tuple[float, float] | None = None

    def _input(self, ep_attribute: str, attribute_name: str) -> Any:
        """Read a cached value of a sibling cluster."""
        cluster = getattr(self.endpoint, ep_attribute, None)
        if cluster is None:
            return None
        return cluster.get(attribute_name)

    def _calculated(self, attribute_name: str) -> float | None:
        """Read a cached calculated value in its real unit."""
        value = self.get(attribute_name)
        if value is None:
            return None
        return value / CALCULATED_SCALE

    def _set(
        self, attribute_name: str, value: float | None, scale: int = CALCULATED_SCALE
    ) -> None:
        """Store a calculated value as an integer, in tenths unless `scale` says otherwise."""
        if value is None:
            return
        self._update_attribute(
            self.find_attribute(attribute_name).id, round(value * scale)
        )

    def _update_rain_rate(self, precipitation: float) -> None:
        """Rain rate in mm/h from the precipitation counter, sampled at least a minute apart."""
        now = time.monotonic()
        history = self._precipitation_history

        if history is None:
            # No history after a restart: keep the last known rate until the next sample
            self._precipitation_history = (precipitation, now)
            if self.get("rain_rate") is None:
                self._set("rain_rate", 0)
            return

        last_value, last_time = history
        elapsed = now - last_time
        if elapsed < RAIN_RATE_MIN_INTERVAL:
            return

        self._precipitation_history = (precipitation, now)
        delta = precipitation - last_value
        if delta < 0:
            # The counter was reset (battery change, restart), start over on the new counter
            self._set("rain_rate", 0)
            return

        rate = delta / (elapsed / 3600)
        self._set("rain_rate", min(_round1(rate), RAIN_RATE_MAX))

    def _update_pressure_trend(self, pressure: float) -> None:
        """Pressure trend in hPa/h, sampled at least 30 minutes apart."""
        now = time.monotonic()
        history = self._pressure_history

        if history is None:
            # No history after a restart: keep the last known trend until the next sample
            self._pressure_history = (pressure, now)
            if self.get("pressure_trend") is None:
                self._set("pressure_trend", 0)
            return

        last_value, last_time = history
        elapsed = now - last_time
        if elapsed < PRESSURE_TREND_MIN_INTERVAL:
            return

        self._pressure_history = (pressure, now)
        self._set("pressure_trend", _round1((pressure - last_value) / (elapsed / 3600)))

    def recalculate(self, source: str) -> None:
        """Update the calculated values after the input `source` changed."""
        temp_raw = self._input("temperature", "measured_value")
        humidity_raw = self._input("humidity", "measured_value")
        pressure_raw = self._input("pressure", "measured_value")
        illuminance_raw = self._input("illuminance", "measured_value")
        wind_raw = self._input("shelly_wind_cluster", "wind_speed")
        precipitation_raw = self._input("shelly_rain_cluster", "precipitation")
        rain_status = self._input("shelly_rain_cluster", "rain_status")

        temp = None if temp_raw is None else temp_raw / 100
        humidity = None if humidity_raw is None else humidity_raw / 100
        # The ZCL pressure measured value is in units of 0.1 kPa, which is 1 hPa
        pressure = None if pressure_raw is None else float(pressure_raw)
        lux = None if illuminance_raw is None else illuminance_lux(illuminance_raw)
        wind = None if wind_raw is None else wind_raw / 10
        precipitation = None if precipitation_raw is None else precipitation_raw / 10

        if source == "precipitation" and precipitation is not None:
            self._update_rain_rate(precipitation)
        if source == "pressure" and pressure is not None:
            self._update_pressure_trend(pressure)

        if temp is not None and humidity is not None:
            self._set("dew_point", calculate_dew_point(temp, humidity))
            self._set("humidex", calculate_humidex(temp, humidity))
            self._set(
                "heat_stress",
                calculate_heat_stress(temp, humidity, lux, wind, bool(rain_status)),
                scale=1,
            )
        if temp is not None and wind is not None:
            self._set("wind_chill", calculate_wind_chill(temp, wind))
        if temp is not None:
            self._set(
                "apparent_temperature",
                calculate_apparent_temperature(temp, humidity, wind),
            )
        if lux is not None:
            condition = calculate_weather_condition(
                temp=temp,
                lux=lux,
                raining=bool(rain_status),
                wind_ms=wind,
                rain_rate=self._calculated("rain_rate"),
                pressure=pressure,
                pressure_trend=self._calculated("pressure_trend"),
            )
            self._set("weather_condition", condition.value, scale=1)


class WS90InputMixin:
    """Drop readings that cannot be real, and trigger the calculations on new ones."""

    # Short name of the input as used by ShellyWS90CalculatedCluster.recalculate
    CALCULATION_SOURCE: str = ""

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """Return True if the value cannot be a real reading, compared to the last one."""
        return False

    def _update_attribute(self, attrid: int | t.uint16_t, value: Any) -> None:
        """Ignore unusable readings, and update the calculated values after a new one."""
        try:
            attribute_name = self.find_attribute(attrid).name
        except KeyError:
            super()._update_attribute(attrid, value)
            return

        if self._is_glitch(attribute_name, value):
            self.debug("ignoring %s reading %s", attribute_name, value)
            return

        super()._update_attribute(attrid, value)

        calculated = getattr(
            self.endpoint, ShellyWS90CalculatedCluster.ep_attribute, None
        )
        if calculated is not None:
            calculated.recalculate(self._calculation_source(attribute_name))

    def _calculation_source(self, attribute_name: str) -> str:
        """Return the input name for an attribute."""
        return self.CALCULATION_SOURCE or attribute_name


class ShellyWindCluster(WS90InputMixin, CustomCluster):
    """Wind measurement cluster for Shelly devices."""

    cluster_id = 0xFC01
    name = "Shelly Wind Cluster"
    ep_attribute = "shelly_wind_cluster"

    class AttributeDefs(BaseAttributeDefs):
        """Wind cluster attribute definitions."""

        wind_speed = ZCLAttributeDef(
            id=0x0000,
            type=t.uint16_t,
            access="rp",
            manufacturer_code=SHELLY_MANUFACTURER_CODE,
        )
        wind_direction = ZCLAttributeDef(
            id=0x0004,
            type=t.uint16_t,
            access="rp",
            manufacturer_code=SHELLY_MANUFACTURER_CODE,
        )
        gust_speed = ZCLAttributeDef(
            id=0x0007,
            type=t.uint16_t,
            access="rp",
            manufacturer_code=SHELLY_MANUFACTURER_CODE,
        )

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """Return True for the 0xFFFF the station sends when a wind reading is unavailable."""
        return value == NON_VALUE_UINT16


class ShellyUVCluster(WS90InputMixin, CustomCluster):
    """UV index measurement cluster for Shelly devices."""

    cluster_id = 0xFC02
    name = "Shelly UV Cluster"
    ep_attribute = "shelly_uv_cluster"

    class AttributeDefs(BaseAttributeDefs):
        """UV cluster attribute definitions."""

        uv_index = ZCLAttributeDef(
            id=0x0000,
            type=t.uint8_t,
            access="rp",
            manufacturer_code=SHELLY_MANUFACTURER_CODE,
        )

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """Return True for the 0xFF the station sends when the UV reading is unavailable."""
        return value == NON_VALUE_UINT8


class ShellyRainCluster(WS90InputMixin, CustomCluster):
    """Rain measurement cluster for Shelly devices."""

    cluster_id = 0xFC03
    name = "Shelly Rain Cluster"
    ep_attribute = "shelly_rain_cluster"

    class AttributeDefs(BaseAttributeDefs):
        """Rain cluster attribute definitions."""

        rain_status = ZCLAttributeDef(
            id=0x0000,
            type=t.Bool,
            access="rp",
            manufacturer_code=SHELLY_MANUFACTURER_CODE,
        )
        precipitation = ZCLAttributeDef(
            id=0x0001,
            type=t.uint24_t,
            access="rp",
            manufacturer_code=SHELLY_MANUFACTURER_CODE,
        )

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """Drop the non-value marker, and a counter that falls to 0 for a moment.

        The station now and then sends a frame with every measurement at 0. A real
        counter reset starts at 0.1 or 0.2, not 0, and a dropped zero would otherwise be
        counted as the whole total again by Home Assistant (total increasing).
        """
        if attribute_name != "precipitation":
            return False
        if value == NON_VALUE_UINT24:
            return True
        previous = self.get(attribute_name)
        return value == 0 and previous is not None and previous > 0


class ShellyWS90Temperature(WS90InputMixin, CustomCluster, TemperatureMeasurement):
    """Temperature measurement, dropping the all-zero frames."""

    CALCULATION_SOURCE = "temperature"

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """0 °C while the previous reading was more than 2 °C away from it."""
        if attribute_name != "measured_value":
            return False
        previous = self.get(attribute_name)
        return value == 0 and previous is not None and abs(previous) > 200


class ShellyWS90Humidity(WS90InputMixin, CustomCluster, RelativeHumidity):
    """Relative humidity measurement, dropping the all-zero frames."""

    CALCULATION_SOURCE = "humidity"

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """0 % while the previous reading was above 5 %."""
        if attribute_name != "measured_value":
            return False
        previous = self.get(attribute_name)
        return value == 0 and previous is not None and previous > 500


class ShellyWS90Pressure(WS90InputMixin, CustomCluster, PressureMeasurement):
    """Pressure measurement, dropping the all-zero frames."""

    CALCULATION_SOURCE = "pressure"

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """Return True for a pressure of 0 hPa, which is never real."""
        return attribute_name == "measured_value" and value == 0


class ShellyWS90Illuminance(WS90InputMixin, CustomCluster, IlluminanceMeasurement):
    """Illuminance measurement, dropping the all-zero frames."""

    CALCULATION_SOURCE = "illuminance"

    def _is_glitch(self, attribute_name: str, value: Any) -> bool:
        """0 lx while the previous reading was above 50 lx."""
        if attribute_name != "measured_value":
            return False
        previous = self.get(attribute_name)
        return value == 0 and previous is not None and illuminance_lux(previous) > 50


CALCULATED = ShellyWS90CalculatedCluster

(
    QuirkBuilder("Shelly", "Ecowitt WS90")
    .replaces(ShellyWindCluster)
    .replaces(ShellyUVCluster)
    .replaces(ShellyRainCluster)
    .replaces(ShellyWS90Temperature)
    .replaces(ShellyWS90Humidity)
    .replaces(ShellyWS90Pressure)
    .replaces(ShellyWS90Illuminance)
    .adds(ShellyWS90CalculatedCluster)
    .sensor(
        attribute_name=ShellyWindCluster.AttributeDefs.wind_speed.name,
        cluster_id=ShellyWindCluster.cluster_id,
        divisor=10,
        unit=UnitOfSpeed.METERS_PER_SECOND,
        device_class=SensorDeviceClass.WIND_SPEED,
        state_class=SensorStateClass.MEASUREMENT,
        reporting_config=ReportingConfig(
            min_interval=10, max_interval=900, reportable_change=5
        ),
        fallback_name="Wind speed",
    )
    .sensor(
        attribute_name=ShellyWindCluster.AttributeDefs.wind_direction.name,
        cluster_id=ShellyWindCluster.cluster_id,
        divisor=10,
        unit=DEGREE,
        device_class=SensorDeviceClass.WIND_DIRECTION,
        state_class=SensorStateClass.MEASUREMENT_ANGLE,
        reporting_config=ReportingConfig(
            min_interval=10, max_interval=900, reportable_change=50
        ),
        fallback_name="Wind direction",
    )
    .sensor(
        attribute_name=ShellyWindCluster.AttributeDefs.gust_speed.name,
        cluster_id=ShellyWindCluster.cluster_id,
        divisor=10,
        unit=UnitOfSpeed.METERS_PER_SECOND,
        device_class=SensorDeviceClass.WIND_SPEED,
        state_class=SensorStateClass.MEASUREMENT,
        reporting_config=ReportingConfig(
            min_interval=10, max_interval=900, reportable_change=10
        ),
        translation_key="gust_speed",
        fallback_name="Gust speed",
    )
    .sensor(
        attribute_name=ShellyUVCluster.AttributeDefs.uv_index.name,
        cluster_id=ShellyUVCluster.cluster_id,
        divisor=10,
        state_class=SensorStateClass.MEASUREMENT,
        reporting_config=ReportingConfig(
            min_interval=300, max_interval=900, reportable_change=1
        ),
        translation_key="uv_index",
        fallback_name="UV index",
    )
    .sensor(
        attribute_name=ShellyRainCluster.AttributeDefs.precipitation.name,
        cluster_id=ShellyRainCluster.cluster_id,
        divisor=10,
        unit=UnitOfPrecipitationDepth.MILLIMETERS,
        device_class=SensorDeviceClass.PRECIPITATION,
        state_class=SensorStateClass.TOTAL_INCREASING,
        reporting_config=ReportingConfig(
            min_interval=300, max_interval=900, reportable_change=1
        ),
        fallback_name="Precipitation",
    )
    .binary_sensor(
        attribute_name=ShellyRainCluster.AttributeDefs.rain_status.name,
        cluster_id=ShellyRainCluster.cluster_id,
        device_class=BinarySensorDeviceClass.MOISTURE,
        reporting_config=ReportingConfig(
            min_interval=0, max_interval=900, reportable_change=1
        ),
        translation_key="rain_detected",
        fallback_name="Rain detected",
    )
    # Calculated values
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.dew_point.name,
        cluster_id=CALCULATED.cluster_id,
        divisor=CALCULATED_SCALE,
        unit=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE,
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="dew_point",
        fallback_name="Dew point",
    )
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.humidex.name,
        cluster_id=CALCULATED.cluster_id,
        divisor=CALCULATED_SCALE,
        unit=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE,
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="humidex",
        fallback_name="Humidex",
    )
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.wind_chill.name,
        cluster_id=CALCULATED.cluster_id,
        divisor=CALCULATED_SCALE,
        unit=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE,
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="wind_chill",
        fallback_name="Wind chill",
    )
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.apparent_temperature.name,
        cluster_id=CALCULATED.cluster_id,
        divisor=CALCULATED_SCALE,
        unit=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE,
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="apparent_temperature",
        fallback_name="Apparent temperature",
    )
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.heat_stress.name,
        cluster_id=CALCULATED.cluster_id,
        unit=PERCENTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="heat_stress",
        fallback_name="Heat stress",
    )
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.rain_rate.name,
        cluster_id=CALCULATED.cluster_id,
        divisor=CALCULATED_SCALE,
        unit=UnitOfVolumetricFlux.MILLIMETERS_PER_HOUR,
        device_class=SensorDeviceClass.PRECIPITATION_INTENSITY,
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="rain_rate",
        fallback_name="Rain rate",
    )
    .sensor(
        attribute_name=CALCULATED.AttributeDefs.pressure_trend.name,
        cluster_id=CALCULATED.cluster_id,
        divisor=CALCULATED_SCALE,
        unit="hPa/h",
        state_class=SensorStateClass.MEASUREMENT,
        translation_key="pressure_trend",
        fallback_name="Pressure trend",
    )
    .enum(
        attribute_name=CALCULATED.AttributeDefs.weather_condition.name,
        enum_class=WeatherCondition,
        cluster_id=CALCULATED.cluster_id,
        entity_platform=EntityPlatform.SENSOR,
        entity_type=EntityType.STANDARD,
        translation_key="weather_condition",
        fallback_name="Weather condition",
    )
    # Battery and solar capacitor voltage
    .sensor(
        attribute_name=PowerConfiguration.AttributeDefs.battery_voltage.name,
        cluster_id=PowerConfiguration.cluster_id,
        divisor=10,
        unit=UnitOfElectricPotential.VOLT,
        device_class=SensorDeviceClass.VOLTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        entity_type=EntityType.DIAGNOSTIC,
        reporting_config=ReportingConfig(
            min_interval=3600, max_interval=65000, reportable_change=1
        ),
        translation_key="battery_voltage",
        fallback_name="Battery voltage",
    )
    .sensor(
        attribute_name=PowerConfiguration.AttributeDefs.battery_2_voltage.name,
        cluster_id=PowerConfiguration.cluster_id,
        divisor=10,
        unit=UnitOfElectricPotential.VOLT,
        device_class=SensorDeviceClass.VOLTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        entity_type=EntityType.DIAGNOSTIC,
        reporting_config=ReportingConfig(
            min_interval=10, max_interval=3600, reportable_change=1
        ),
        translation_key="capacitor_voltage",
        fallback_name="Capacitor voltage",
    )
    .add_to_registry()
)
