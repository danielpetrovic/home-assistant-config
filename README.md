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
- Shelly Wall Display XL in living room for HA control, alarm, and Unifi Protect camera feeds

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
