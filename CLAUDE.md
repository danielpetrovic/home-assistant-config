# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
│   ├── danielpetrovic/            # Author: danielpetrovic (4 blueprints)
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

**Lights:**
- `_light` - Main room lights
- `_above_light` / `_below_light` - Position-based variants
- `_bed_light` - Bed-specific lights
- `_cabinet` - Cabinet lights

**Covers:**
- `_shade` - Roller shades
- `_shutter` - Window shutters
- `_curtain` - Curtains

**Climate:**
- `_heating` - Heating thermostats and switches
- `_ac` - Air conditioning

**Sensors:**
- `_door_contact` - Door contact sensors
- `_motion_occupancy` - Motion occupancy sensors
- `_temperature` - Temperature sensors
- `_illuminance` - Light level sensors

**Helpers:**
- `_covers` - Cover preset selectors (input_select)
- `_position` - Cover position sliders (input_number)
- `_button_last_pressed` - Button state tracking (input_text)

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

### 3. Alarm System with Keypad (3,704 lines)
- Author: danielpetrovic
- Multi-keypad: Z2M, ZHA, Dashboard
- Multi-user PIN + RFID support
- Two-tier validation (user → master code)
- Auto-arm/disarm with geofencing
- Rich notifications with camera feeds
- Emergency SOS button
- Dashboard keypad scripts with dash separator naming
- Requires HA 2024.8.0+

### 4. Niko Battery Switch (Multi-Button) (~3,800 lines)
- Author: danielpetrovic
- Models: 552-720X1 (1-button), 552-720X2 (2-button), 552-720X4 (4-button)
- On/Off, long-press dimming, double-press custom
- Schedule-based automation with auto-adjust
- Z2M and ZHA unified handling via normalization layer
- Loop-based dimming with smooth transitions (universal compatibility)
- No helper requirements (removed in v2026.01.9)
- Auto-detects Z2M elapsed field for improved double-click reliability
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

### Music Presets (3)
- `music_away` - Turns off all 7 media players
- `music_night` - Plays Classical playlist on bedroom (leader) grouped with kitchen & gym, volume 0.25
- `music_home` - Plays 9 playlists (Asian, Dance, Eurovision, Hip Hop, Jazz, Metal, Pop, R&B, Rock) on bedroom (leader) grouped with kitchen & gym, different volumes per room (0.35 kitchen, 0.25 bedroom/gym)

### Lighting Presets (6)
- `lights_preset_000` - Turn off light
- `lights_preset_025` - 25% brightness with adaptive color
- `lights_preset_050` - 50% brightness with adaptive color
- `lights_preset_100` - 100% brightness with adaptive color
- `lights_preset_adaptive` - Full adaptive (brightness + color)
- `lights_preset_adaptive_color` - Adaptive color temp only (keeps current brightness)

### Lighting Effects (4)
- `lights_green` - Breathing animation: Green
- `lights_blue` - Breathing animation: Blue
- `lights_red` - Breathing animation: Red
- `lights_orange` - Breathing animation: Orange

### Alarm Mode Scripts (5)
- `alarm_away` - Armed Away mode actions
- `alarm_home` - Armed Home mode actions (stops siren)
- `alarm_night` - Armed Night mode actions (stops siren)
- `alarm_disarmed` - Disarmed mode actions (stops siren)
- `alarm_pending` - Pending mode actions (triggers siren)

### Ventilation Presets (3)
- `fan_away` - Sets ventilation to Away mode
- `fan_home` - Sets ventilation to Auto mode
- `fan_night` - Sets ventilation to Manual 2 Forced

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

## Common Commands

### Configuration Management

```bash
# Validate configuration before restarting
ha core check

# Restart Home Assistant
ha core restart

# Reload specific components without full restart
ha core reload --automation
ha core reload --script
ha core reload --template

# Check Home Assistant version
ha core info

# Update Home Assistant
ha core update
```

### Logs and Debugging

```bash
# View live logs
ha core logs

# Follow logs in real-time
ha core logs -f

# View supervisor logs
ha supervisor logs

# Check specific integration logs
grep "zigbee2mqtt" /config/home-assistant.log
grep "adaptive_lighting" /config/home-assistant.log

# Clear log file
> /config/home-assistant.log
```

### Automation Testing

```bash
# Trigger automation manually (via Developer Tools > Services)
# Service: automation.trigger
# Entity: automation.<automation_name>

# Enable/disable automation
ha core service call automation.turn_off --entity automation.<automation_name>
ha core service call automation.turn_on --entity automation.<automation_name>

# Reload automations after editing
ha core reload --automation

# Test templates (via Developer Tools > Template)
# Navigate to: http://homeassistant.local:8123/developer-tools/template
```

### Backup and Restore

```bash
# Create backup
ha backups new --name "manual_backup_$(date +%Y%m%d)"

# List backups
ha backups list

# Restore from backup
ha backups restore <backup_slug>
```

### Add-on Management

```bash
# List installed add-ons
ha addons

# Start/stop add-on
ha addons start <addon_name>
ha addons stop <addon_name>

# Restart specific add-on (e.g., Zigbee2MQTT)
ha addons restart zigbee2mqtt

# View add-on logs
ha addons logs zigbee2mqtt
```

## Remote Access Setup

### SSH Connection Details

```bash
Host: <your-ha-hostname>
User: root
Port: 22
Path: /config
```

### rclone Configuration

The Home Assistant config directory is accessible remotely via rclone with SFTP backend.

**Setup:**

```bash
# Configure rclone (interactive)
rclone config

# Remote name: ha
# Type: sftp
# Host: <your-ha-hostname>
# User: root
# Port: 22
# SSH key: ~/.ssh/id_ed25519
```

**Common Operations:**

```bash
# List files
rclone ls ha:/config

# List directories
rclone lsd ha:/config

# Copy file from remote to local
rclone copy ha:/config/configuration.yaml ~/local_backup/

# Copy file from local to remote
rclone copy ~/local_backup/configuration.yaml ha:/config/

# Sync entire config (use with caution!)
rclone sync ha:/config ~/ha-config-backup/

# View file contents
rclone cat ha:/config/configuration.yaml

# Edit file remotely (download, edit, upload)
rclone copy ha:/config/automations.yaml ~/automations.yaml
# ... edit locally ...
rclone copy ~/automations.yaml ha:/config/automations.yaml
```

**Note:** FUSE mounting via `rclone mount` is not available in Termux/Android environments. Use direct rclone commands instead.

### SSH Direct Access

```bash
# Connect to HA server
ssh root@<your-ha-hostname>

# Execute remote command
ssh root@<your-ha-hostname> "ha core info"

# Copy files with scp
scp root@<your-ha-hostname>:/config/configuration.yaml ~/backup/

# Copy files with rsync
rsync -avz root@<your-ha-hostname>:/config/ ~/ha-backup/
```

## Testing & Debugging

### Configuration Validation

**Before any restart:**

```bash
# Always validate configuration first
ha core check

# Expected output: "Configuration valid!"
# If errors are found, they will be listed with line numbers
```

### Blueprint Testing

**Testing Custom Blueprints:**

1. **Syntax Validation:**
   - Check YAML syntax: `ha core check`
   - Errors will reference the blueprint file path

2. **Import Test:**
   - Navigate to: Settings > Automations & Scenes > Blueprints
   - Click "Import Blueprint" and verify it appears

3. **Automation Creation:**
   - Create a test automation using the blueprint
   - Fill in all required inputs
   - Save and trigger manually via Developer Tools

4. **Trace Mode:**
   - Navigate to: Settings > Automations & Scenes
   - Select your test automation
   - Click "Traces" to see execution history
   - Review each step's execution time and output

### Common Troubleshooting

**Automation Not Triggering:**

```bash
# Check if automation is enabled
# Navigate to: Settings > Automations & Scenes
# Look for disabled automations (greyed out)

# Review automation traces
# Click on automation > Traces tab
# Check "Last Triggered" timestamp

# Check condition evaluation
# Traces will show which conditions passed/failed
```

**Integration Issues:**

```bash
# Check logs for specific integration
ha core logs | grep "integration_name"

# Common integrations to monitor:
# - zigbee2mqtt
# - adaptive_lighting
# - music_assistant
# - unifiprotect

# Restart integration (via Developer Tools > Services)
# Service: homeassistant.reload_config_entry
# Entity: config_entry_id (found in .storage/core.config_entries)
```

**Template Errors:**

```bash
# Test templates in Developer Tools > Template
# Navigate to: http://homeassistant.local:8123/developer-tools/template

# Common template debugging:
{{ states('sensor.outdoor_temperature') }}
{{ state_attr('climate.living_room_heating', 'current_temperature') }}
{{ is_state('binary_sensor.living_room_motion_occupancy', 'on') }}

# Check template sensor logs
ha core logs | grep "template"
```

**Performance Issues:**

```bash
# Check database size
ls -lh /config/home-assistant_v2.db

# If database is large (>1GB), consider:
# - Reducing recorder history (configuration.yaml)
# - Excluding chatty sensors from recorder
# - Running database maintenance

# Check system resources
ha host info

# Check add-on resource usage
ha addons stats
```

**Z2M Device Issues:**

```bash
# Check Zigbee2MQTT logs
ha addons logs zigbee2mqtt

# Common issues:
# - Device not responding: Check battery level
# - Pairing fails: Reset device and try again
# - Firmware updates: Check Z2M dashboard for available updates

# Restart Zigbee2MQTT
ha addons restart zigbee2mqtt
```

### Log File Locations

```
/config/home-assistant.log           # Main HA log
/config/home-assistant.log.1         # Previous log (rotated)
/config/home-assistant.log.fault     # Fault/crash logs
/addon_configs/*/                    # Add-on specific configs
```

### Debugging Workflow

1. **Reproduce the issue** - Trigger the automation/integration manually
2. **Check logs** - `ha core logs` or grep for specific integration
3. **Review traces** - For automations, check trace timeline
4. **Validate config** - `ha core check` before making changes
5. **Test in isolation** - Disable other automations that might interfere
6. **Incremental changes** - Change one thing at a time, test after each change
7. **Use trace mode** - Enable trace logging for detailed execution flow
