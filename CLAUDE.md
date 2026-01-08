@.claude/CLAUDE.md

# Home Assistant Configuration - Context for AI Assistants

This repository contains a Home Assistant configuration for a residential smart home installation.

## System Overview

- **Home Assistant Version:** 2025.12.4
- **Primary Integration:** Zigbee2MQTT (Z2M)
- **Configuration Style:** Package-based with YAML automations
- **Network:** Trusted proxy configuration for reverse proxy access

## Repository Structure

```
config/
├── configuration.yaml             # Core HA config (uses packages)
├── automations.yaml               # 69 automations (5,669 lines)
├── scripts.yaml                   # Music & lighting scripts (559 lines)
├── .storage/                      # HA storage (mostly gitignored)
│   ├── lovelace                   # Main dashboard (committed)
│   ├── lovelace.backup            # Dashboard backup (committed)
│   ├── lovelace.dashboard_tablet  # Tablet dashboard (committed)
│   ├── lovelace_dashboards        # Dashboard config (committed)
│   └── lovelace_resources         # Custom resources (committed)
├── packages/                      # Modular configuration
│   ├── alarm.yaml                 # Manual alarm panel with keypad
│   ├── buttons.yaml               # Button press state tracking
│   ├── climate.yaml               # Climate control, weather sensors & schedules
│   ├── covers.yaml                # Window covering presets
│   ├── duco.yaml                  # DucoBox ventilation REST API
│   ├── lights.yaml                # Adaptive lighting system & sensors
│   └── system.yaml                # System-wide helpers & counters
├── blueprints/automation/         # Custom automation blueprints
│   ├── danielpetrovic/            # Author: danielpetrovic (3 blueprints)
│   └── panhans/                   # Author: panhans (1 blueprint)
└── themes/danielpetrovic/         # 3 UI theme variants
```

## Areas (18 Total - 3 Floors)

The installation is organized across 3 floors:

### Ground Floor (BG) - 10 Areas

| Area | Icon | Devices |
|------|------|---------|
| Dining Table | mdi:table-furniture | Lighting, controls |
| Entrance | mdi:coat-rack | Motion sensors |
| Kitchen | mdi:countertop-outline | Media, sensors |
| Alleyway | mdi:walk | Motion sensors |
| Utility Closet | mdi:server-network | Network equipment |
| Shed | mdi:garage | Outdoor equipment |
| Toilet | mdi:toilet | Door sensors |
| Garden | mdi:flower | Outdoor devices |
| Front Door | mdi:door-closed | Security |
| Living Room | mdi:sofa | Full automation zone |

### First Floor (01) - 5 Areas

| Area | Icon | Devices |
|------|------|---------|
| Bathroom | mdi:shower-head | Climate, lighting |
| Hallway | mdi:stairs | Motion, lighting |
| Office | mdi:desk | Climate, media, automation |
| Gym | mdi:dumbbell | Climate, media, lighting |
| Bedroom | mdi:bed-king | Climate, dual sensors |

### Second Floor (02) - 3 Areas

| Area | Icon | Devices |
|------|------|---------|
| Roof Terrace | mdi:balcony | Outdoor controls |
| Gameroom | mdi:controller | Climate, media, automation |
| Laundry Room | mdi:washing-machine | Lighting, sensors |

## Naming Conventions

### Standard Pattern

`<location>_<device_type>` (e.g., `light.living_room_light`, `climate.office_heating`)

### Device Type Suffixes
- `_light` / `_light_lower` - Lights (upper/lower variants)
- `_heating` - Heating
- `_ac` - Air conditioning
- `_curtain` - Curtains (Somfy RTS)
- `_blind` - Blinds (Somfy IO)
- `_door_sensor_contact` - Door sensors
- `_motion` - Motion sensors

## Key Integrations

### Zigbee2MQTT (Z2M)
- Primary protocol for switches, sensors, lights
- OTA firmware updates for Zigbee devices
- Philips Hue Tap Dial switches
- Niko battery switches (552-720X1)

### Music Assistant

7 KEF wireless speakers integrated via Music Assistant:

| Location | Model | Notes |
|----------|-------|-------|
| Bathroom | LSX II LT | Bookshelf |
| Gameroom | LSX II | Bookshelf |
| Office | LSX II | Bookshelf |
| Kitchen | LSX II | Bookshelf |
| Gym | LSX II | Bookshelf |
| Bedroom | LSX II | Bookshelf |
| Living Room | XIO Soundbar | 5.1.2 soundbar system |

**Features:**
- Playlist management (Classical, Dance, Eurovision, Hip Hop, Jazz, Metal, Pop, R&B, Rock)
- Coordinated multi-room playback with grouping
- KEF custom integration via [hass-kef-connector](https://github.com/your-repo/hass-kef-connector)
- Profile-based EQ management per speaker

### Unifi Protect
- Security camera system integration
- Live camera feeds displayed on Shelly Wall Display XL (living room)
- Integrated with alarm system for notifications

### DucoBox Ventilation (REST API)
- REST-based integration via HTTP
- 7 sensor nodes (temp, humidity, CO2)
- 6 preset modes: Auto, Away, Manual 1/2/3 (with forced variants)
- Room monitoring: Living Room, Bedroom, Gym, Office, Gameroom, Bathroom

### Climate Control
- **6 Heating Zones:** Bathroom, Gameroom, Office, Gym, Bedroom, Living Room
- **4 AC Units:** Gameroom, Office, Bedroom, Living Room
- **3 Underfloor Heating Circulation Pumps:** Monitored via HomeWizard Energy Sockets (Heating Pump 0, 1, 2)
- Generic thermostat platform (1h min cycle, 0.5°C step, 16-22°C range)
- Schedule-based activation (weekday/weekend patterns)

**Weather Statistics Sensors:**
- `sensor.outdoor_luminosity` - 5-minute mean of illuminance (source: Ecowitt WS90 Powered by Shelly via Zigbee)
- `sensor.outdoor_temperature_max` - 96-hour maximum outdoor temperature
- `sensor.outdoor_temperature_min` - 168-hour (7-day) minimum outdoor temperature
- `sensor.outdoor_temperature_mean` - 168-hour (7-day) mean outdoor temperature

**Climate Template Sensors:**
- `sensor.cooling` - Count of AC units actively cooling
- `sensor.heating` - Count of heating zones actively heating
- `sensor.daylight_duration` - Hours between sunrise and sunset
- `sensor.climate_heat_stress` - Calculated heat stress percentage (0-100%) based on temperature, solar radiation, humidity, wind, and rain
- `sensor.climate_humidex` - Humidex calculation (feels-like temperature in °C)
- `sensor.climate_luminosity` - Categorized outdoor brightness (dark/low/mid/high/peak)
- `sensor.climate_mode` - State machine for seasonal climate mode (warm/mild/cold) with hysteresis

### System Sensors

**Entity Counters:**
- `sensor.lights` - Automatic count of all lights currently on (dynamic, no hardcoded list)
- `sensor.switches` - Automatic count of all switches currently on (dynamic, no hardcoded list)

**Configuration Helpers:**
- `input_text.base_url` - Home Assistant base URL for external access

### HomeWizard Energy Monitoring (25+ devices)

**P1 Meter (1):**
- Main electricity meter (P1 port)

**Wi-Fi kWh Meters 1-phase (9):**
- Solar panels
- Air conditioning
- Shed
- Instant hot water tap
- Gym
- Dishwasher
- Combination oven
- Steam oven
- Utility closet

**Wi-Fi Energy Sockets (13):**
- Washing machine
- Dryer
- Kitchen media + Fridge + Freezer
- Heat recovery ventilation
- Living room media
- Bedroom media
- Gameroom media
- Alex PC
- Deni PC
- Large freezer
- Heating pump 0, 1, 2 (Underfloor heating circulation pumps)

**Wi-Fi Watermeter (1):**
- Water meter (Water consumption)

## Automation Architecture

### 69 Total Automations Organized by Category

1. **Media/Entertainment (5)** - Time-based and on-demand playback
2. **Window Coverings (20 covers, 10 locations)** - Somfy RTS (curtains) & Somfy IO (blinds)
3. **Door Sensors (5)** - Light triggers and state tracking
4. **Motion Detection (9)** - Selected rooms with motion sensors (not all rooms)
5. **Heating Control (3)** - Underfloor heating circulation pump automation
6. **Button Control (17)** - Philips Hue Tap Dial + Niko switches
7. **Energy Monitoring (25+ devices)** - Comprehensive HomeWizard monitoring
8. **Climate Control (6)** - Schedule-based HVAC
9. **Specialized (7+)** - Alarm, vacuum, ventilation, adaptive lighting

### Automation Patterns
- **Mode:** Restart (prevents queuing)
- **Triggers:** State changes, time-based, motion, zone changes
- **Conditions:** Presence (zone.home), door state, time branches
- **Actions:** Scenes, delays, choose/conditions with aliases

## Custom Blueprints

### 1. Adaptive Lighting Scheduler (975 lines)
- Author: danielpetrovic
- Calculates brightness/color temp throughout day
- Sunrise/sunset awareness
- Schedule-based with different weekday/weekend patterns
- Stores values in `input_number` helpers
- Used by: `lights_bright` and `lights_dimmed` schedules

### 2. Philips Hue Tap Dial Switch (4,334 lines)
- Author: danielpetrovic
- Z2M and ZHA support
- 4 buttons + rotary dial
- Context-aware modes: Custom, Light, Media
- Schedule integration with auto-adjust
- Requires HA 2024.10.0+

### 3. Alarm System with Keypad (3,651 lines)
- Author: danielpetrovic
- Multi-keypad: Z2M, ZHA, Dashboard
- Multi-user PIN + RFID support
- Two-tier validation (user → master code)
- Auto-arm/disarm with geofencing
- Rich notifications with camera feeds
- Emergency SOS button
- Requires HA 2024.8.0+

### 4. Niko Battery Switch (Multi-Button) (~1011 lines)
- Author: danielpetrovic
- Models: 552-720X1 (1-button), 552-720X2 (2-button), 552-720X4 (4-button)
- On/Off, long-press dimming, double-press custom
- Schedule-based automation with auto-adjust
- Z2M simulated brightness support (auto-detected, smooth continuous dimming)
- Auto-detects Z2M elapsed field for improved double-click reliability
- No helper requirements (removed in v2026.01.9)
- Unified 300ms double-press timeout (ZHA & Z2M)
- Requires HA 2024.10.0+

## Adaptive Lighting System

Uses the [Adaptive Lighting](https://github.com/basnijholt/adaptive-lighting) integration with two profiles:
- **`lights_bright`** - Higher brightness levels
- **`lights_dimmed`** - Lower brightness levels

**Configuration:**
- Automatic brightness and color temperature adjustment based on time of day
- Color Temperature: 2200K (night) → 4000K (daytime)
- Sunrise/sunset awareness
- Per-light configuration with profiles

**Note:** Legacy schedule helpers still exist in configuration but are no longer actively used.

## Climate Schedules

| Location | Weekdays | Weekends |
|----------|----------|----------|
| Bathroom/Gym | 06:00-10:00 | 06:00-10:00 |
| Office/Gameroom | 06:00-18:00 | Off |
| Bedroom | 06:00-10:00, 20:00-24:00 | 06:00-10:00, 20:00-24:00 |
| Living Room | 06:00-10:00, 16:00-24:00 | 06:00-24:00 |

## Alarm System

- **Control Panel:** Manual alarm panel
- **Users:** Multiple user codes + master code
- **Keypads:** Dashboard implementation (10 digits + backspace/clear)
- **Presence Detection:** zone.home integration
- **Scene Management:** Dynamic snapshot/restore
- **Visual Cues:** 4 breathing effects (Green, Blue, Red, Orange)

## Scripts

### Lighting Presets (8)
- `000%`, `025%`, `050%`, `100%` - Fixed brightness with adaptive color
- `Adaptive` - Full adaptive (brightness + color)
- `Adaptive Color` - Color temp only

### Music Presets (3)
- `Music Away` - Turns off all 7 media players
- `Music Night` - Plays Classical playlist on bedroom (leader) grouped with kitchen & sauna, volume 0.25
- `Music Home` - Plays 9 playlists (Asian, Dance, Eurovision, Hip Hop, Jazz, Metal, Pop, R&B, Rock) on bedroom (leader) grouped with kitchen & sauna, different volumes per room (0.35 kitchen, 0.25 bedroom/sauna)

### Alarm Effects (4)
- Breathing animations: Green, Blue, Red, Orange

## Development Patterns

### Configuration Philosophy
- **All helpers in YAML:** Template sensors, statistics sensors, and input helpers are defined in package files, not through the GUI
- **YAML-first approach:** GUI helpers have been migrated to YAML for version control and documentation
- **Preserve unique_id:** When migrating from GUI to YAML, always keep the exact same `unique_id` to maintain historical data

### When Adding Automations
1. Follow naming: `<location>_<function>` (e.g., `Living Room Climate`)
2. Use mode: restart (unless specific queuing needed)
3. Leverage blueprints for common patterns
4. Add aliases to choose actions for clarity

### When Adding Devices
1. Follow naming convention: `<location>_<device_type>`
2. Add to appropriate package file
3. Update relevant schedules if time-based
4. Consider blueprint integration

### When Adding Helpers
1. Define in appropriate package file (not GUI)
2. Use template sensors with `unique_id` for entity registry tracking
3. Add `state_class: measurement` for numeric sensors that need long-term statistics
4. Statistics sensors inherit unit from source sensor (don't specify `unit_of_measurement`)

### When Modifying Schedules
1. Update both `lights_bright` and `lights_dimmed` if adaptive lighting
2. Maintain weekday/weekend patterns
3. Test auto-adjust in blueprints

### Package Organization
- **alarm.yaml** - Security and access control
- **buttons.yaml** - Physical button state tracking
- **climate.yaml** - HVAC thermostats and schedules, weather statistics sensors (outdoor luminosity, temperature min/max/mean), climate template sensors (daylight duration, heat stress, humidex, luminosity categories, climate mode)
- **covers.yaml** - Window covering presets
- **duco.yaml** - Ventilation system integration
- **lights.yaml** - Adaptive lighting helpers and schedules, automatic light counter sensor
- **system.yaml** - System-wide helpers (base URL input_text), automatic entity counters (switches)

## Git Practices

- **Gitignored:** .storage/ (except dashboards), custom_components/, secrets.yaml, *.db, backups/
- **Committed:** All configuration YAML, packages, blueprints, themes, dashboards
- **Dashboard Files:** Lovelace dashboards stored in .storage/lovelace* are committed for sharing
- **Recent Focus:** Airplay configuration, adaptive lighting, Z2M optimization

## User Profiles

- Multiple users configured with individual alarm codes
- Presence tracking via zone.home for automation
- Geofencing support for auto-arm/disarm

## Control Interfaces

### Shelly Wall Display XL (Living Room)
- **Location:** Living Room
- **Functions:**
  - Full Home Assistant dashboard access
  - Alarm control panel
  - Live camera feeds from Unifi Protect
  - Central control point for entire home
- **Integration:** Native HA display integration

### Dashboards
- **Storage:** `.storage/lovelace*` files (committed to repo)
- **Types:**
  - Main dashboard (lovelace)
  - Tablet dashboard (lovelace.dashboard_tablet)
  - Dashboard configuration (lovelace_dashboards)
  - Custom resources (lovelace_resources)
- **Format:** YAML-based Lovelace UI configuration
