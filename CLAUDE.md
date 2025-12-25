# Home Assistant Configuration - Context for AI Assistants

This repository contains a Home Assistant configuration for a Dutch residential smart home installation.

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
│   ├── climate.yaml               # Thermostats & schedules
│   ├── covers.yaml                # Window covering presets
│   ├── lights.yaml                # Adaptive lighting system
│   └── duco.yaml                  # DucoBox ventilation REST API
├── blueprints/automation/         # Custom automation blueprints
│   ├── danielpetrovic/            # Author: danielpetrovic (3 blueprints)
│   └── panhans/                   # Author: panhans (1 blueprint)
└── themes/danielpetrovic/         # 3 UI theme variants
```

## Areas (18 Total - 3 Floors)

The installation uses Dutch area naming organized across 3 floors:

### Ground Floor (BG) - 10 Areas

| Dutch | English | Icon | Devices |
|-------|---------|------|---------|
| Eettafel | Dining Table | mdi:table-furniture | Lighting, controls |
| Entree | Entrance | mdi:coat-rack | Motion sensors |
| Keuken | Kitchen | mdi:countertop-outline | Media, sensors |
| Looppad | Alleyway | mdi:walk | Motion sensors |
| Meterkast | Utility Closet | mdi:server-network | Network equipment |
| Schuur | Shed | mdi:garage | Outdoor equipment |
| Toilet | Toilet | mdi:toilet | Door sensors |
| Tuin | Garden | mdi:flower | Outdoor devices |
| Voordeur | Front Door | mdi:door-closed | Security |
| Woonkamer | Living Room | mdi:sofa | Full automation zone |

### First Floor (01) - 5 Areas

| Dutch | English | Icon | Devices |
|-------|---------|------|---------|
| Badkamer | Bathroom | mdi:shower-head | Climate, lighting |
| Gang | Hallway | mdi:stairs | Motion, lighting |
| Kantoor | Office | mdi:desk | Climate, media, automation |
| Sauna | Sauna | mdi:dumbbell | Climate, media, lighting |
| Slaapkamer | Bedroom | mdi:bed-king | Climate, dual sensors |

### Second Floor (02) - 3 Areas

| Dutch | English | Icon | Devices |
|-------|---------|------|---------|
| Dakterras | Roof Terrace | mdi:balcony | Outdoor controls |
| Gameroom | Gameroom | mdi:controller | Climate, media, automation |
| Wasruimte | Laundry Room | mdi:washing-machine | Lighting, sensors |

## Naming Conventions

### Standard Pattern

`<location>_<device_type>` (e.g., `light.woonkamer_lamp`, `climate.kantoor_verwarming`)

### Device Type Suffixes
- `_lamp` / `_lamp_onder` - Lights (upper/lower variants)
- `_verwarming` - Heating
- `_airco` - Air conditioning
- `_gordijn` - Curtains (Somfy RTS)
- `_rolluik` - Blinds (Somfy IO)
- `_deursensor_contact` - Door sensors
- `_beweging` - Motion sensors

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
| Badkamer | LSX II LT | Compact model without analogue input |
| Gameroom | LSX II | Full-featured bookshelf |
| Kantoor | LSX II | Full-featured bookshelf |
| Keuken | LSX II | Full-featured bookshelf |
| Sauna | LSX II | Full-featured bookshelf |
| Slaapkamer | LSX II | Full-featured bookshelf |
| Woonkamer | XIO Soundbar | 5.1.2 soundbar system |

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
- Room monitoring: Woonkamer, Slaapkamer, Sauna, Kantoor, Gameroom, Badkamer

### Climate Control
- **6 Heating Zones:** Badkamer, Gameroom, Kantoor, Sauna, Slaapkamer, Woonkamer
- **4 AC Units:** Gameroom, Kantoor, Slaapkamer, Woonkamer
- **3 Underfloor Heating Circulation Pumps:** Monitored via HomeWizard Energy Sockets (Verwarmingspomp 0, 1, 2)
- Generic thermostat platform (1h min cycle, 0.5°C step, 16-22°C range)
- Schedule-based activation (weekday/weekend patterns)

### HomeWizard Energy Monitoring (25+ devices)

**P1 Meter (1):**
- Main electricity meter (P1 port)

**Wi-Fi kWh Meters 1-phase (9):**
- Zonnepanelen (Solar panels)
- Airco (Air conditioning)
- Schuur (Shed)
- Quooker (Instant hot water tap)
- Sauna
- Vaatwasser (Dishwasher)
- Oven Combi (Combination oven)
- Oven Stoom (Steam oven)
- Meterkast (Utility closet)

**Wi-Fi Energy Sockets (13):**
- Wasmachine (Washing machine)
- Wasdroger (Dryer)
- Keuken Media + Koelkast + Vriezer (Kitchen media + Fridge + Freezer)
- WtW (Heat recovery ventilation)
- Woonkamer Media (Living room media)
- Slaapkamer Media (Bedroom media)
- Gameroom Media
- Alex PC
- Deni PC
- Vriezer Groot (Large freezer)
- Verwarmingspomp 0, 1, 2 (Underfloor heating circulation pumps)

**Wi-Fi Watermeter (1):**
- Watermeter (Water consumption)

## Automation Architecture

### 69 Total Automations Organized by Category

1. **Media/Entertainment (5)** - Time-based and on-demand playback
2. **Window Coverings (20 covers, 10 locations)** - Somfy RTS (gordijn/curtains) & Somfy IO (rolluik/blinds)
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

### 4. Niko Battery Switch (894 lines)
- Author: danielpetrovic
- Model: 552-720X1
- On/Off, long-press dimming, double-press custom
- Schedule-based automation with auto-adjust
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
| Badkamer/Sauna | 06:00-10:00 | 06:00-10:00 |
| Kantoor/Gameroom | 06:00-18:00 | Off |
| Slaapkamer | 06:00-10:00, 20:00-24:00 | 06:00-10:00, 20:00-24:00 |
| Woonkamer | 06:00-10:00, 16:00-24:00 | 06:00-24:00 |

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

### When Adding Automations
1. Follow naming: `<location>_<function>` (e.g., `Woonkamer Klimaat`)
2. Use mode: restart (unless specific queuing needed)
3. Leverage blueprints for common patterns
4. Add aliases to choose actions for clarity

### When Adding Devices
1. Follow naming convention: `<location>_<device_type>`
2. Add to appropriate package file
3. Update relevant schedules if time-based
4. Consider blueprint integration

### When Modifying Schedules
1. Update both `lights_bright` and `lights_dimmed` if adaptive lighting
2. Maintain weekday/weekend patterns
3. Test auto-adjust in blueprints

### Package Organization
- **alarm.yaml** - Security and access control
- **buttons.yaml** - Physical button state tracking
- **climate.yaml** - HVAC thermostats and schedules
- **covers.yaml** - Window covering presets
- **lights.yaml** - Adaptive lighting helpers and schedules
- **duco.yaml** - Ventilation system integration

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
- **Location:** Woonkamer (Living Room)
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
