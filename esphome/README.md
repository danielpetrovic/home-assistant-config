# Somfy RTS Bridge (ESPHome)

Local, cloud-free control of Somfy RTS shades/curtains from Home Assistant, running on a [LilyGO TTGO T3 LoRa32 433MHz V1.6.1](https://lilygo.cc/products/lora3) ESP32 board — no extra hardware required, it drives the board's onboard SX1276 radio directly in raw OOK mode.

Built as an alternative/complement to a Somfy TaHoma box: each cover gets its own virtual remote, added to the physical motor alongside its existing TaHoma pairing (non-destructive — TaHoma keeps working as a fallback).

**Scope:** Somfy **RTS** only (one-way, curtains/shades). Somfy **IO** (shutters, two-way) is a different, more complex protocol and is not covered here.

## Hardware

- LilyGO TTGO T3 LoRa32 433MHz V1.6.1 (SX1276 radio, 433MHz variant — must match your region's RTS frequency)
- Nothing else. On this exact board revision, DIO2 is wired to GPIO32, which is what ESPHome's `sx127x` OOK mode needs — all pins used in `somfy-rts-bridge.yaml` are already on-board.

## How it works

- `sx127x` (ESPHome core) puts the SX1276 into raw OOK mode at 433.42MHz (Somfy's exact carrier — not the 433.92MHz ISM default).
- `remote_transmitter`/`remote_receiver` (ESPHome core) bit-bang the OOK data through the shared GPIO32/DIO2 pin.
- [`swoboda1337/somfy-esphome`](https://github.com/swoboda1337/somfy-esphome) (external component) implements the actual Somfy RTS protocol — rolling codes, frame encoding — on top of that radio. Ported from [`Legion2/Somfy_Remote_Lib`](https://github.com/Legion2/Somfy_Remote_Lib).
- Each cover is a `template` cover with a simplified three-state position model — no time-based position estimation, no drift:
  - **0%** → `DOWN` (fully closed)
  - **50%** → `MY` (the motor's own favorite/preset position — whatever that's set to)
  - **100%** → `UP` (fully open)
  - Stop always sends `MY` too, matching how a real Somfy remote's MY button behaves (stops in place if moving, jumps to the preset if idle).

## Files

- `somfy-rts-bridge.yaml` — the device config: radio setup, Wi-Fi/API/OTA, the OLED display, and one `packages:` entry per physical cover.
- `somfy_rts_cover.yaml` — reusable package template (one virtual remote + a "Pair" button + the cover logic above), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `remote_address`, `device_class`).
- `secrets.yaml` (gitignored) — Wi-Fi credentials, API encryption key, OTA password.

## Adding a new cover

Add another entry under `packages:` in `somfy-rts-bridge.yaml`:
```yaml
  my_new_cover: !include
    file: somfy_rts_cover.yaml
    vars:
      cover_id: my_new_cover
      cover_name: "My New Cover"
      remote_address: "0x<fresh random 24-bit hex, never reused>"
      device_class: shade # or curtain
```

## Pairing a cover to its motor

Each virtual remote has to be added to its motor the normal Somfy way:
1. Put the motor into programming mode via an **already-paired** remote (hold its PROG button until the motor jogs).
2. Within a couple of seconds, press the new cover's `<Cover Name> Pair` button in Home Assistant.
3. The motor jogs again, confirming the new remote is paired — in addition to, not instead of, any existing remote (e.g. TaHoma).

## OLED display

The onboard SSD1306 cycles through a page per cover (name + position %) every 3 seconds, ending on a status page (device name + IP).

## Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **433MHz** V1.6.1 specifically. Pin numbers throughout (`spi:`, `sx127x:`, `i2c:`) are for that exact board revision — check your board's pinout before reusing this on anything else.
