# Somfy Bridges (ESPHome)

Local, cloud-free control of Somfy covers from Home Assistant - no TaHoma/Connexoon box required for either bridge, and both work *alongside* an existing TaHoma too (non-destructive: adding a bridge's virtual remote doesn't remove any existing pairing).

> [!CAUTION]
> Educational/personal use only, on devices you own. Interacting with RTS or io-homecontrol devices that aren't yours may be illegal in your jurisdiction. Provided as-is, no warranty - see the licenses of the upstream projects this is built on.

Two independent bridges, each on its own physical board (different frequency, different protocol, different hardware). Which bridge you need depends on which **communication protocol** your specific motor uses, not the physical device type - Somfy sells shutters, shades, curtains and awnings on *both* RTS and IO, so it's entirely possible to have RTS shutters, IO curtains, or any other combination. Check your motor/remote's own documentation (or the frequency printed on it) to know which protocol it actually speaks.

| | Somfy RTS Bridge | Somfy IO Bridge |
|---|---|---|
| Protocol | RTS (one-way, rolling code) | io-homecontrol, 1W (one-way) subset |
| Frequency | 433MHz | 868MHz |
| Board | LilyGO TTGO T3 LoRa32 433MHz V1.6.1 | LilyGO TTGO T3 LoRa32 868MHz V1.6.1 |
| Status | Stable | Actively developed - see Known Issues |
| Config file | `somfy-rts-bridge.yaml` | `somfy-io-bridge.yaml` |

### Cover device classes

Every cover on both bridges takes a standard ESPHome/Home Assistant `device_class`, set per-cover in YAML to match whatever's physically installed (the motor/protocol has no way to know this itself):

| `device_class` | Home Assistant meaning |
|---|---|
| `awning` | Awning |
| `blind` | Blind (slats that tilt, e.g. venetian) |
| `curtain` | Curtain |
| `damper` | Damper (HVAC airflow) |
| `door` | Generic door |
| `garage` | Garage door |
| `gate` | Gate |
| `shade` | Shade / roller shade (no tilt) |
| `shutter` | Shutter |
| `window` | Window |

The examples below use this installation's actual types (RTS: `curtain`/`shade`, IO: `shutter`) - substitute whichever matches your own devices.

---

## Somfy RTS Bridge

Drives the board's onboard SX1276 radio directly in raw OOK mode. No extra hardware required.

### Setup

1. **Hardware**: LilyGO TTGO T3 LoRa32 **433MHz** V1.6.1 - nothing else needed, the onboard radio and its pin wiring (DIO2→GPIO32) are already correct for this exact board revision.
2. **Secrets**: add to `secrets.yaml` (create it from the template below if it doesn't exist yet):
   ```yaml
   wifi_ssid: "your_wifi_name"
   wifi_password: "your_wifi_password"
   rts_bridge_api_key: "base64-encoded-32-byte-key"   # generate via `esphome secrets` or any base64(32 random bytes)
   rts_bridge_ota_password: "some-password"
   ```
3. **Add an area + device entry** for each physical cover under `esphome:` in `somfy-rts-bridge.yaml` - this is what makes the cover show up as its own HA device (matching Overkiz's per-shade/curtain granularity) instead of bunched onto the hub:
   ```yaml
   esphome:
     areas:
       - id: my_room_area
         name: "My Room" # must match an existing HA area name exactly, or HA creates a new one
     devices:
       - id: my_new_cover_device
         name: "My Room Curtain" # include the room name here - HA's own Area-prefix-to-entity_id feature isn't reliable enough to depend on for display naming
         area_id: my_room_area
   ```
4. **Add a cover entry** for each physical cover under `packages:` in `somfy-rts-bridge.yaml` (`cover_id` + `_device` must match the device `id` above):
   ```yaml
   packages:
     my_new_cover: !include
       file: somfy-rts-cover.yaml
       vars:
         cover_id: my_new_cover
         cover_name: "My New Cover"
         remote_address: "0x<fresh random 24-bit hex, never reused across covers>"
         device_class: curtain # or shade, awning, blind, window, etc. - see "Cover device classes" above
   ```
5. **Flash** `somfy-rts-bridge.yaml` via the ESPHome dashboard (USB for the first flash, OTA afterward).
6. **Pair each cover to its motor** (see below).
7. **Test**: Open/Close/Stop from the cover's card in Home Assistant.

### Pairing a cover to its motor

Each virtual remote has to be added to its motor the normal Somfy way:
1. Put the motor into programming mode via an **already-paired** remote (hold its PROG button until the motor jogs).
2. Within a couple of seconds, press the new cover's `<Cover Name> Pair` button in Home Assistant.
3. The motor jogs again, confirming the new remote is paired, in addition to (not instead of) any existing remote (e.g. TaHoma).

### Modes

Each cover has a `select:` entity to switch between:
- **`My`** (default) - three fixed states, no time-based estimation, no drift:
  - **0%** → `DOWN` (fully closed)
  - **50%** → `MY` (the motor's own favorite/preset position, whatever that's set to)
  - **100%** → `UP` (fully open)
  - Stop always sends `MY` too, matching how a real Somfy remote's MY button behaves (stops in place if moving, jumps to the preset if idle).
- **`Timed`** - local travel-time-based position estimate (`Travel Time Open`/`Travel Time Close` number entities, per-cover, default 25s each, adjustable live from Home Assistant - no reflash needed). Approximate; can drift on repeated partial moves since RTS is one-way and the motor never reports real position back. Requesting an intermediate position sends `UP`/`DOWN` and auto-stops (`MY`) once the estimate reaches the target.

### How it works

- `sx127x` (ESPHome core) puts the SX1276 into raw OOK mode at 433.42MHz (Somfy's exact carrier, not the 433.92MHz ISM default).
- `remote_transmitter`/`remote_receiver` (ESPHome core) bit-bang the OOK data through the shared GPIO32/DIO2 pin.
- [`swoboda1337/somfy-esphome`](https://github.com/swoboda1337/somfy-esphome) (external component) implements the actual Somfy RTS protocol (rolling codes, frame encoding) on top of that radio. Ported from [`Legion2/Somfy_Remote_Lib`](https://github.com/Legion2/Somfy_Remote_Lib).
- Each cover is a `template` cover - see [Modes](#modes) above for the two position models it can switch between.

### Files

- `somfy-rts-bridge.yaml`: the device config (radio setup, Wi-Fi/API/OTA, the OLED display, `bluetooth_proxy`, and one `packages:` entry per physical cover).
- `somfy-rts-cover.yaml`: reusable package template (virtual remote, Pair button, Mode select, Travel Time Open/Close numbers, and the cover logic above), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `remote_address`, `device_class`, `travel_time_open`, `travel_time_close`).

### Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **433MHz** V1.6.1 specifically. Pin numbers throughout (`spi:`, `sx127x:`, `i2c:`) are for that exact board revision, check your board's pinout before reusing this on anything else.

---

## Somfy IO Bridge

Drives the board's onboard SX1276 radio directly at the register level (no ESPHome core radio component covers io-homecontrol's FSK framing), via a vendored port of a real working implementation rather than a from-scratch reimplementation.

### Setup

1. **Hardware**: LilyGO TTGO T3 LoRa32 **868MHz** V1.6.1 - nothing else needed. Pin mapping (confirmed against the official schematic) lives in `components/iohc/iohc_board_config.h`.
2. **Secrets**: add to `secrets.yaml`:
   ```yaml
   wifi_ssid: "your_wifi_name"
   wifi_password: "your_wifi_password"
   io_bridge_api_key: "base64-encoded-32-byte-key"
   io_bridge_ota_password: "some-password"
   ```
3. **Add a cover entry** for each physical cover under `packages:` in `somfy-io-bridge.yaml`. This is all you need to get going - everything else has a sensible default:
   ```yaml
   packages:
     my_new_cover_io: !include
       file: somfy-io-cover.yaml
       vars:
         cover_id: my_new_cover_io
         cover_name: "My New Cover IO"
   ```
   This creates the cover plus its "Pair"/"Unpair" buttons and "Mode" select, with a random bonded identity auto-generated and stored on the board's own flash the first time it boots. See `somfy-io-cover.yaml` for the full set of optional vars (`device_class`, defaults to `shutter` - see [Cover device classes](#cover-device-classes); `broadcast_type`, `travel_time_open`/`travel_time_close` - rarely need overriding).
4. **(Recommended) Switch to a fixed identity**, so a future board replacement doesn't require re-pairing every motor - see [Board-independent identity](#board-independent-identity-surviving-a-board-replacement) below for why, and a ready-to-run command to generate one. Can be done any time, including after the cover is already paired (unpair first, then switch, then re-pair).
5. **Flash** `somfy-io-bridge.yaml` via the ESPHome dashboard (USB for the first flash, OTA afterward).
6. **Pair each cover to its motor** (see below).
7. **Test**: Open/Close/Stop from the cover's card in Home Assistant. Leave the mode `select` on its default (`1W My`) unless you specifically want the time-based estimate - see [Modes](#modes).

### Pairing a cover to its motor

Same physical procedure as RTS:
1. Put the motor into programming mode via an **already-paired** remote (hold its PROG button until the motor jogs).
2. Within a couple of seconds, press the new cover's `<Cover Name> Pair` button in Home Assistant (transmits `Add`, cmd `0x30` - the actual key-exchange command; despite the friendly "Pair" label, this is *not* the same as `RemoteButton::Pair`/cmd `0x2e`, which only applies once a key is already shared).
3. The motor jogs again, confirming the new virtual remote is bonded, in addition to (not instead of) any existing remote.

To remove a bonded identity (e.g. before migrating to a fixed node/key), press the `<Cover Name> Unpair` button first and confirm the motor stops responding to it before proceeding - there's no acknowledgment in this protocol, so this is the only way to verify it actually took.

### Board-independent identity (surviving a board replacement)

By default, each cover generates a **random** virtual remote identity (a 3-byte node address + 16-byte AES key) on first boot, stored only in that specific board's flash (NVS). If the board ever dies, a replacement would generate a *different* random identity, requiring every motor to be re-paired.

To avoid that, generate a fixed identity for the cover:
```shell
python3 -c "import secrets; print('node:', secrets.token_hex(3)); print('key:', secrets.token_hex(16))"
```
add the printed values to `secrets.yaml`, named after the cover:
```yaml
io_<cover_id>_node: "<the printed node>"
io_<cover_id>_key: "<the printed key>"
```
and reference them in the cover's `!include` vars:
```yaml
node: !secret io_<cover_id>_node
key: !secret io_<cover_id>_key
```
A replacement board flashed with the same config reproduces the exact same identity - no re-pairing needed. If a cover is already paired with a random identity, unpair it first before switching to a fixed one, then pair again.

### Modes

Each cover has a `select:` entity to switch between:
- **`1W Timed`** - local travel-time-based position estimate (`Travel Time Open`/`Travel Time Close` number entities, per-cover, default 25s each, adjustable live from Home Assistant - persists across reboots, no reflash needed). Approximate; can drift on repeated partial moves since the motor doesn't report real position back.
- **`1W My`** (default) - matches the RTS bridge's own model exactly: three discrete states (0% closed / 50% "My"-or-stopped / 100% open), no time tracking, no drift. Any position request strictly between 0 and 100 maps to the physical My/Stop button.
- **`2W`** - placeholder for real motor-reported position. Not implemented; selecting it just logs a warning and ignores commands.

### Broadcast type caveat

`broadcast_type` (per cover, default `0` = "All") controls which device-class group the `Add`/`Pair` broadcast targets - see `sDevicesType` in `components/iohc/iohc_utils.h` for the full list. The default matches upstream's own default but isn't independently verified against every motor model; if `Add` doesn't take, trying a different value (e.g. `2` = "Roller shutter") is the next thing to check.

### Scope and status

**Implemented (1W, one-way commands):**
- Real-time passive reception/decode of io-homecontrol traffic (RSSI per source address, logged).
- Pairing (`Add`) and unpairing (`Remove`) a virtual remote identity against a real motor.
- Open / Close / Stop, and absolute Position (0-100%) commands, confirmed working against real hardware.
- Per-cover selectable mode (`1W Timed` / `1W My` / `2W` placeholder).

**Known issues:**
- **"My" (go to favorite position) doesn't reliably work while the cover is idle**, even though the identical byte sequence sent by a real Situo remote does. Open/Close/Stop (including stopping active movement) all work correctly. Current best lead: our own transmissions aren't repeating (a real Situo remote repeats each frame ~4 times plus a follow-up confirmation frame; ours currently sends each once), and the repeat/retry state machine in `iohcRadio.cpp` is a fragile hand-rolled ISR+timer construct whose debug output goes straight to the UART, invisible over network logs - needs a live serial session to chase further.
- **2W (two-way, real position feedback) is not implemented.** `CMD 4` status frames from real 2W motors are passively logged but not decrypted - that requires the actual AES challenge/response layer, a substantially larger undertaking than the 1W command layer here (upstream's own project describes 2W as unstable even in their hands).

### Files

- `somfy-io-bridge.yaml`: the device config (radio setup, Wi-Fi/API/OTA, OLED display, and one `packages:` entry per physical cover).
- `somfy-io-cover.yaml`: reusable package template (cover + Pair/Unpair buttons + Mode select + Travel Time Open/Close numbers), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `device_class`, `node`, `key`, `broadcast_type`, `travel_time_open`, `travel_time_close`).
- `components/iohc/`: the local `external_component`.
  - Flat directory (no subdirectories except `cover/`, `button/`, `select/` - the only structure ESPHome's local-component loader actually supports) - see the comment in `iohc.h` for why.
  - `iohcRadio.*`, `iohcPacket.*`, `SX1276Helpers.*`, `sx1276Regs-Fsk.h`, `TickerUsESP32.*`, `Delegate.h`: vendored radio/protocol layer, near-verbatim from upstream.
  - `iohc_remote1w.*`: the command/pairing layer (Add/Remove/Open/Close/Stop/Position), rewritten around ESPHome's `Preferences`-backed persistence instead of upstream's JSON-file + MQTT model.
  - `iohc_blind_position.*`: the optional local travel-time position estimator (`1W Timed` mode only).
  - `cover/`, `button/`, `select/`: the ESPHome platform integration.

### Attribution

Built on [`rspaargaren/iohomecontrol`](https://github.com/rspaargaren/iohomecontrol) (Apache-2.0), itself building on [`Velocet/iown-homecontrol`](https://github.com/Velocet/iown-homecontrol) (protocol documentation and reverse-engineering) and [`cridp/iown-homecontrol-esp32sx1276`](https://github.com/cridp/iown-homecontrol-esp32sx1276) (SX1276 register handling). The vendored/ported files live in `components/iohc/` with their original copyright headers intact; see the comment at the top of each file for what was ported near-verbatim versus rewritten for ESPHome.

### Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **868MHz** V1.6.1 specifically. Requires the `arduino` framework (not `esp-idf`, used by the RTS bridge) since the vendored radio code uses Arduino's `SPI`/`Preferences` libraries directly - both are declared under `esphome: libraries:` in `somfy-io-bridge.yaml` since PlatformIO's dependency finder doesn't pick them up automatically from a local `external_component`.

---

## OLED display

Both bridges' onboard SSD1306 cycle through a status page (packet count / RSSI, or per-cover name + position, depending on the bridge) plus a page showing the device's IP.
