# 🏠 Home Assistant Configuration

My personal Home Assistant configuration for a comprehensive smart home installation.

## 📊 Quick Stats

- **Automations:** 69 automations across multiple rooms
- **Blueprints:** 4 custom blueprints
- **Areas:** 18 areas across 3 floors with climate, lighting, and media control
- **Primary Protocol:** Zigbee via Zigbee2MQTT (Z2M)

## ⚡ Key Features

### Adaptive Lighting
- Uses the [Adaptive Lighting](https://github.com/basnijholt/adaptive-lighting) integration
- Two profiles: bright and dimmed modes
- Automatic brightness and color temperature adjustment throughout the day
- Sunrise/sunset awareness

### Climate Control
- 6 heating zones with schedule-based activation
- 4 air conditioning units
- DucoBox ventilation system with REST API integration
- CO2 and temperature monitoring across 6 rooms

### Security
- Multi-user alarm system with PIN and RFID support
- Dashboard keypad implementation
- Geofencing with auto-arm/disarm
- Visual and audible alerts

### Media
- 7 distributed speakers via Music Assistant
- Multi-room synchronized playback with grouping
- Playlist management (Classical, Dance, Eurovision, Hip Hop, Jazz, Metal, Pop, R&B, Rock)
- Scenario-based playback (Away, Night, Home)

### Automation
- Button control (Philips Hue Tap Dial + Niko switches)
- Motion-based lighting (selected rooms)
- Automated window coverings (20 covers across 10 locations)
- Energy monitoring (25+ HomeWizard devices)
- Samsung Tab A11+ wall tablet with HA Companion App (set as Home/Launcher) for full HA control, alarm, and Unifi Protect camera feeds

## 📁 Repository Structure

```
config/
├── configuration.yaml        # Core configuration with packages
├── automations.yaml          # 69 automations
├── scripts.yaml              # Lighting and music presets
├── .storage/                 # Dashboard configurations (committed)
│   ├── lovelace
│   ├── lovelace.backup
│   ├── lovelace.dashboard_tablet
│   ├── lovelace_dashboards
│   └── lovelace_resources
├── packages/                 # Modular configuration
│   ├── alarm.yaml
│   ├── buttons.yaml
│   ├── climate.yaml
│   ├── covers.yaml
│   ├── lights.yaml
│   └── duco.yaml
├── blueprints/               # Custom automation blueprints
└── themes/                   # UI themes
```

## 🪟 Window Coverings

20 covers across 10 locations, controlled via `input_select` presets. Each room's automation triggers whenever the preset changes.

### Preset Order
`Closed → Privacy → Shaded → Exposed → Open` (not all rooms have all presets)

- **Exposed** — lets sunlight/heat in (passive solar gain)
- **Shaded** — blocks direct sun while keeping some light
- **Privacy** — near-closed, maintains privacy
- **Closed** — fully closed

### Cover Positions by Room

#### Floor 00

| Room | Preset | Shade/Curtain | Shutter(s) |
|------|--------|---------------|------------|
| **Garden** | Open | 100% | 100% |
| | Exposed | 50% | 90% |
| | Shaded | 50% | 80% |
| | Closed | 50% | 0% |
| **Kitchen** | Open | 100% | 100% |
| | Exposed | 50% | 90% |
| | Shaded | 50% | 80% |
| | Closed | 50% | 0% |
| **Living Room** | Open | 100% | 100% |
| | Exposed | 50% | 90% |
| | Shaded | 50% | 50% |
| | Privacy | 50% | 20% |
| | Closed | 50% | 0% |
| **Shed** | Open | — | Alleyway: 100% Garden: 100% |
| | Alleyway | — | Alleyway: 100% Garden: 0% |
| | Garden | — | Alleyway: 0% Garden: 100% |
| | Closed | — | Alleyway: 0% Garden: 0% |

#### Floor 01

| Room | Preset | Shade/Curtain | Shutter(s) |
|------|--------|---------------|------------|
| **Bedroom** | Open | 100% | L/M/R: 100% |
| | Shaded | 0% | L/M/R: 90% |
| | Privacy | 0% | L/M/R: 20% |
| | Closed | 0% | L/M/R: 0% |
| **Gym** | Open | 100% | 100% |
| | Shaded | 0% | 90% |
| | Privacy | 0% | 20% |
| | Closed | 0% | 0% |
| **Hallway** | Open | 100% | 100% |
| | Exposed | 50% | 90% |
| | Shaded | 0% | 50% |
| | Privacy | 0% | 20% |
| | Closed | 0% | 0% |
| **Office** | Open | 100% | 100% |
| | Exposed | 50% | 90% |
| | Shaded | 50% | 50% |
| | Privacy | 0% | 20% |
| | Closed | 0% | 0% |

#### Floor 02

| Room | Preset | Shade/Curtain | Shutter(s) |
|------|--------|---------------|------------|
| **Gameroom** | Open | 100% | L:100% R:100% |
| | Exposed | 50% | L:50% R:90% |
| | Shaded | 50% | L:25% R:50% |
| | Privacy | 50% | L:20% R:20% |
| | Closed | 0% | L:0% R:0% |
| **Roof Terrace** | Open | 100% | 100% |
| | Exposed | 50% | 90% |
| | Shaded | 50% | 80% |
| | Closed | 0% | 0% |

### Climate-based Automation

The `Covers` automation adjusts presets automatically based on `sensor.climate_mode` and `sensor.outdoor_brightness`:

| Condition | Gameroom / Office / Living Room | Garden / Kitchen / Roof Terrace |
|-----------|--------------------------------|---------------------------------|
| warm/hot + sunny | Privacy | Shaded |
| warm/hot + overcast/bright | Shaded | Shaded |
| warm/hot + dark/dim | Shaded | Exposed |
| freezing/cold/mild + daylight ≥ 12h + bright/sunny | **Shaded** | Exposed |
| freezing/cold/mild + dark/dim/overcast | Exposed | Exposed |
| freezing/cold/mild + daylight < 12h (winter) | Exposed | Exposed |

## 🌡️ Climate Control

### Heating Zones (6)

Schedule-based activation via inline time triggers. Presence required in all heating windows.

| Zone | Weekdays | Weekends |
|------|----------|----------|
| Bathroom | 06:00–10:00 | 06:00–10:00 |
| Gym | 06:00–10:00 | 06:00–10:00 |
| Office | 06:00–18:00 | Off |
| Gameroom | 06:00–18:00 | Off |
| Bedroom | 06:00–10:00, 20:00–24:00 | 06:00–10:00, 20:00–24:00 |
| Living Room | 06:00–10:00, 16:00–24:00 | 06:00–24:00 |

### Cooling / AC Setpoints (4 zones)

All AC zones turn off when `sensor.climate_mode` is freezing or cold, and when no presence is detected.

> **Daikin offset:** Daikin units cool to approximately 2°C below the set temperature (setpoint 28°C → effective ~26°C).

| Zone | Active cooling trigger | Active setpoint | Passive setpoint | Sleep setpoint |
|------|----------------------|-----------------|------------------|----------------|
| Bathroom / Gym | — (no AC) | — | — | — |
| Office | `desk_power > 40W` + presence (any time, any day) | warm→28°C, hot→26°C | warm/mild→30°C, hot→28°C | — |
| Gameroom | `desk_power > 40W` OR `media_power > 20W` + presence (any time, any day) | warm→28°C, hot→26°C | warm/mild→30°C, hot→28°C | — |
| Bedroom | 22:00–01:00 or 06:00–08:00 + presence | warm→28°C, hot→26°C | warm/mild→30°C, hot→28°C | 26°C during 01:00–06:00 (warm/hot + presence) |
| Living Room | `media_power > 50W` + presence (any time, any day) | warm→28°C, hot→26°C | warm/mild→30°C, hot→28°C | — |

**Effective temperatures (setpoint − 2°C Daikin offset):**

| Mode | Setpoint | Effective |
|------|----------|-----------|
| warm active | 28°C | ~26°C |
| hot active | 26°C | ~24°C |
| warm/mild passive | 30°C | ~28°C |
| hot passive | 28°C | ~26°C |
| sleep (bedroom) | 26°C | ~24°C |

**Power thresholds:** Office/Gameroom desk: 40W (standby spikes to 36W). Gameroom media: 20W. Living Room media: 50W (soft standby 25–31W, active viewing 100–150W).

## 🎨 Custom Blueprints

1. **Adaptive Lighting Scheduler** - Dynamic brightness/color temperature throughout the day
2. **Philips Hue Tap Dial Switch** - 4 buttons + rotary dial with context-aware modes
3. **Alarm System with Keypad** - Multi-user security system with geofencing
4. **Niko Battery Switch** - Schedule-based button automation

## 📖 Documentation

- See [CLAUDE.md](CLAUDE.md) for detailed technical documentation
- Configuration is organized using the package system for modularity
- All automations follow consistent naming: `<location>_<function>`

## 🔄 Updates

This repository is regularly updated with changes to my Home Assistant installation.
