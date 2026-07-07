# Somfy Bridges (ESPHome)

Local, cloud-free control of Somfy covers from Home Assistant - no TaHoma/Connexoon box required for either bridge.

> [!IMPORTANT]
> Everything already paired to a motor keeps working exactly as before. A bridge just adds itself as one more virtual remote alongside whatever's already there - existing physical remotes, wall switches, sensors, and TaHoma/Connexoon all keep working unchanged; nothing is removed or replaced. This matters in practice because a motor's memory for how many controllers it can remember is small and finite:
> - **RTS**: motors can remember up to **12 "controllers"** total - a broader category than just remotes. Wall switches, timers, group memberships, and sensors (e.g. light/sun or rain sensors) all consume a slot too, same as a remote does - **and TaHoma/Connexoon itself takes one too**, it's just another paired controller from the motor's point of view. Adding one of this bridge's virtual remotes uses up one more of those 12 slots.
> - **IO (io-homecontrol)**: Somfy doesn't publish an equivalent number for IO motors. The protocol itself does track a paired-controller count internally, but no public maximum could be found for this project - if you're already close to whatever your specific motor's limit turns out to be, pairing one more (including this bridge) could fail or push out an existing one. Same relationship as RTS: **TaHoma/Connexoon is just another paired controller here too**, no special exemption from whatever the limit turns out to be.

> [!CAUTION]
> Personal use only, on devices you own. Interacting with RTS or IO (io-homecontrol) devices that aren't yours may be illegal in your jurisdiction. Provided as-is, with no guarantees of any kind - see the licenses of the upstream projects this is built on.

Two independent bridges, each on its own physical board (different frequency, different protocol, different hardware). Which bridge you need depends on which **communication protocol** your specific motor uses, not the physical device type - Somfy sells shutters, shades, curtains and awnings on *both* RTS and IO, so it's entirely possible to have RTS shutters, IO curtains, or any other combination. Check your motor/remote's own documentation (or the frequency printed on it) to know which protocol it actually speaks.

"IO" here is Somfy's own branding for their io-homecontrol devices - io-homecontrol is a wider interoperability standard also used by other manufacturers (Velux, Honeywell, etc.), not something Somfy-specific. This bridge has only been built and tested against Somfy IO hardware, even though the underlying protocol layer (`components/iohc/`) is standard io-homecontrol and should, in principle, apply to other manufacturers' io-homecontrol devices too.

| | Somfy RTS Bridge | Somfy IO Bridge |
|---|---|---|
| Protocol | RTS (one-way, rolling code) | IO (io-homecontrol), 1W (one-way) subset |
| Frequency | 433MHz | 868MHz |
| Board | LilyGO TTGO T3 LoRa32 433MHz V1.6.1 | LilyGO TTGO T3 LoRa32 868MHz V1.6.1 |
| Status | Stable | Stable (1W) - 2W not implemented |
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
2. Within a couple of seconds, press the new cover's `<Cover Name> Program` button in Home Assistant.
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

- `somfy-rts-bridge.yaml`: the device config (radio setup, Wi-Fi/API/OTA, the OLED display, `bluetooth_proxy`, diagnostic entities (WiFi Signal, Uptime, Loop Time, Restart Reason, Restart - same as the IO bridge), and one `packages:` entry per physical cover).
- `somfy-rts-cover.yaml`: reusable package template (virtual remote, Program button, My button, Mode select, Travel Time Open/Close numbers, and the cover logic above), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `remote_address`, `device_class`, `travel_time_open`, `travel_time_close`).

### Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **433MHz** V1.6.1 specifically. Pin numbers throughout (`spi:`, `sx127x:`, `i2c:`) are for that exact board revision, check your board's pinout before reusing this on anything else.

---

## Somfy IO Bridge

Drives the board's onboard SX1276 radio directly at the register level (no ESPHome core radio component covers IO's (io-homecontrol's) FSK framing), via a vendored port of a real working implementation rather than a from-scratch reimplementation.

### Setup

1. **Hardware**: LilyGO TTGO T3 LoRa32 **868MHz** V1.6.1 - nothing else needed. Pin mapping (confirmed against the official schematic) lives in `components/iohc/iohc_board_config.h`.
2. **Secrets**: add to `secrets.yaml`:
   ```yaml
   wifi_ssid: "your_wifi_name"
   wifi_password: "your_wifi_password"
   io_bridge_api_key: "base64-encoded-32-byte-key"
   io_bridge_ota_password: "some-password"
   ```
3. **Generate a fixed identity for the new cover** *before* adding it - see [Board-independent identity](#board-independent-identity-surviving-a-board-replacement) below for the command and why this matters. Give every new cover one from the start rather than the random default: switching an already-paired cover from random to fixed later needs an unpair/re-pair cycle, which is easy to just avoid entirely.
4. **Add a cover entry** for each physical cover under `packages:` in `somfy-io-bridge.yaml`, including the `node`/`key` from step 3:
   ```yaml
   packages:
     my_new_cover_io: !include
       file: somfy-io-cover.yaml
       vars:
         cover_id: my_new_cover_io
         cover_name: "My New Cover IO"
         node: !secret io_my_new_cover_io_node
         key: !secret io_my_new_cover_io_key
   ```
   This creates the cover plus its "Program" button (pairs and unpairs, like the physical remote's own PROG button), a "My" button, three Identify buttons (see [Identify](#identify)), and "Mode" select. See `somfy-io-cover.yaml` for the full set of optional vars (`device_class`, defaults to `shutter` - see [Cover device classes](#cover-device-classes); `broadcast_type`, `travel_time_open`/`travel_time_close` - rarely need overriding). `node`/`key` can be left as empty strings instead, in which case a random identity is auto-generated and stored on the board's own flash on first boot - not recommended, see step 3.
5. **Flash** `somfy-io-bridge.yaml` via the ESPHome dashboard (USB for the first flash, OTA afterward).
6. **Pair each cover to its motor** (see below).
7. **Test**: Open/Close/Stop from the cover's card in Home Assistant. Leave the mode `select` on its default (`1W My`) unless you specifically want the time-based estimate - see [Modes](#modes).

### Pairing (and unpairing) a cover to its motor

Same physical procedure as RTS, and a single button either way - matching the physical remote's own PROG button (spelled out as "Program" in Home Assistant, since "PROG" is Somfy's own abbreviation and not obvious to someone unfamiliar with their remotes), which also both pairs and unpairs depending on whether it's already bonded. This matches Somfy's own official instructions for adding/removing a control exactly (see their support articles "How do I add another control?" and "How do I delete the control of a motorized window covering?"): there is no separate hardware-level "unpair" command, only the same PROG signal sent while the motor is listening.

Importantly, this is **not** the motor toggling a shared signal - checked directly against upstream's own reference implementation (`rspaargaren/iohomecontrol`): `Add` (cmd `0x30`) and `Remove` (cmd `0x39`) are two structurally distinct commands, each unconditionally doing what its name says, with no toggle behavior anywhere in the protocol or upstream's own code. A real physical remote has no way to query the motor's live bonding state either - it just remembers its own last known status locally, and sends `Add` or `Remove` accordingly when PROG is pressed. Our Program button reproduces exactly that: it reads our own persisted pairing status (was the last successful Add/Remove for this cover an add or a remove?) and sends the appropriate command.

**To add** (pair a new cover to a motor that already has at least one working control):
1. Put the motor into programming mode via an **already-paired, still-working** remote (hold its PROG button until the motor jogs - can take a few seconds, don't give up too early).
2. Within a couple of seconds, press the new cover's `<Cover Name> Program` button in Home Assistant (transmits `Add`, cmd `0x30` - the actual key-exchange command; despite the label, this is *not* the same as `RemoteButton::Pair`/cmd `0x2e`, which only applies once a key is already shared).
3. The motor jogs again, confirming the change - the new virtual remote is now bonded, in addition to (not instead of) any existing remote (e.g. TaHoma, or a physical Situo).

**To remove** (e.g. before migrating to a fixed node/key, or decommissioning a cover): the procedure is identical, not a separate step - put the motor into programming mode via **any other still-working control** (does not have to be our board), then press the `<Cover Name> Program` button again. Since our own record shows this cover as already bonded, it sends `Remove` instead of `Add`.

> [!WARNING]
> Per Somfy's own instructions: a control can only be removed if **another** control is available to open the motor's programming window first. If our board's Program button is the *only* paired control for a motor and you remove it, you lose the ability to open that motor's programming window at all (nothing left to press PROG on) - you'd need the motor manufacturer's own hard-reset procedure to recover, not something this bridge can help with. Always keep at least one other working control (TaHoma, a physical Situo/Telis, etc.) paired before removing this board's identity.

There's no acknowledgment in this protocol, so our record of whether a cover is currently bonded is an assumption, not a verified fact - if an Add/Remove is sent while the motor wasn't actually in its programming window, our record and the motor's real state fall out of sync (the frame goes nowhere, but we still update our own record as if it worked). Confirming the motor stops responding (after a removal) or starts responding (after an add) is the only way to verify a press actually took; if it doesn't match what you expected, the Program button may need pressing twice to get back in sync.

### Identify

Three extra buttons per cover - `<Cover Name> Identify`, `Start Identify`, `Stop Identify` - jog the motor without changing its position, useful for visually confirming which physical motor a Home Assistant entity actually maps to. `Identify` is a one-shot jog (matches TaHoma's own "locate device" action); `Start`/`Stop Identify` are for a manual continuous blink. Confirmed working against real hardware.

No physical Somfy remote has an Identify button, so there was no confirmed 1W frame format to copy going in - what's implemented is a best-guess signed 1W frame built by matching the command byte (`cmd=0x1E`) and payload bytes seen in a real captured 2W TaHoma-to-motor exchange, wrapped in the same self-signed `data+sequence+hmac` structure our own Pair/Remove frames use (since a 1W broadcast has no live challenge/response round trip to lean on the way 2W does). That guess turned out correct - it works over 1W despite Identify itself only being documented/observed as a 2W (TaHoma) feature.

### Board-independent identity (surviving a board replacement)

By default, each cover generates a **random** virtual remote identity (a 3-byte node address + 16-byte AES key) on first boot, stored only in that specific board's flash (NVS). If the board ever dies, a replacement would generate a *different* random identity, requiring every motor to be re-paired.

Do this **before** pairing a new cover, not after - switching an already-paired cover from random to fixed needs an unpair/re-pair cycle (see step 3 in Setup above). Generate a fixed identity for the cover:
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
- **`1W My`** (default) - three discrete states (0% closed / 50% "My" / 100% open), no time tracking, no drift. Any position request strictly between 0 and 100 sends `Vent` (`main=0xd8`), the real My/favorite-position command - confirmed via a live capture of a real TaHoma "My" press. Unlike RTS, My and Stop are **not** the same command for IO: the dedicated Stop action still sends `Stop` (`main=0xd2`), which (also confirmed via live capture) only has an effect while the motor is actively moving.
- **`2W`** - placeholder for real motor-reported position. Not implemented; selecting it just logs a warning and ignores commands.

### Broadcast type caveat

`broadcast_type` (per cover, default `0` = "All") controls which device-class group the `Add`/`Pair` broadcast targets - see `sDevicesType` in `components/iohc/iohc_utils.h` for the full list. The default is confirmed working against real shutter motors; there should be no need to change it.

### Scope and status

**Implemented (1W, one-way commands):**
- Real-time passive reception/decode of IO traffic (RSSI per source address, logged).
- Pairing and unpairing via a single `Program` button, dispatching to `Add` (cmd `0x30`) or `Remove` (cmd `0x39`) based on our own persisted pairing status - matches how real Somfy remotes and Somfy's own official add/remove instructions actually work, see [Pairing](#pairing-and-unpairing-a-cover-to-its-motor) above.
- Open / Close / Stop / My / absolute Position (0-100%) commands, confirmed working against real hardware. My and Stop are distinct commands (`Vent`/`main=0xd8` vs `Stop`/`main=0xd2`), confirmed via live captures of real TaHoma traffic.
- Identify / Start Identify / Stop Identify (`cmd=0x1E`) - best-guess frames, confirmed working against real hardware, see [Identify](#identify) above.
- Per-cover selectable mode (`1W Timed` / `1W My` / `2W` placeholder).

All of the above confirmed working against real motors, including pairing/unpairing multiple physical shutters end-to-end.

**Not yet implemented:**
- **2W (two-way, real position feedback).** `CMD 4` status frames from real 2W motors are passively logged but not decrypted - that requires the actual AES challenge/response layer, a substantially larger undertaking than the 1W command layer here (upstream's own project describes 2W as unstable even in their hands).

### Files

- `somfy-io-bridge.yaml`: the device config (radio setup, Wi-Fi/API/OTA, OLED display, diagnostic entities (WiFi Signal, Uptime, Loop Time, Restart Reason, Restart - same as the RTS bridge), and one `packages:` entry per physical cover).
- `somfy-io-cover.yaml`: reusable package template (cover + Program button + My button + Identify/Start/Stop Identify buttons + Mode select + Travel Time Open/Close numbers), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `device_class`, `node`, `key`, `broadcast_type`, `travel_time_open`, `travel_time_close`).
- `components/iohc/`: the local `external_component`.
  - Flat directory (no subdirectories except `cover/`, `button/`, `select/` - the only structure ESPHome's local-component loader actually supports) - see the comment in `iohc.h` for why.
  - `iohcRadio.*`, `iohcPacket.*`, `SX1276Helpers.*`, `sx1276Regs-Fsk.h`, `TickerUsESP32.*`, `Delegate.h`: vendored radio/protocol layer, near-verbatim from upstream.
  - `iohc_remote1w.*`: the command/pairing layer (Add/Remove/Open/Close/Stop/Vent/Position/Identify), rewritten around ESPHome's `Preferences`-backed persistence instead of upstream's JSON-file + MQTT model.
  - `iohc_blind_position.*`: the optional local travel-time position estimator (`1W Timed` mode only).
  - `cover/`, `button/`, `select/`: the ESPHome platform integration.

### Attribution

Built on [`rspaargaren/iohomecontrol`](https://github.com/rspaargaren/iohomecontrol) (Apache-2.0), itself building on [`Velocet/iown-homecontrol`](https://github.com/Velocet/iown-homecontrol) (protocol documentation and reverse-engineering) and [`cridp/iown-homecontrol-esp32sx1276`](https://github.com/cridp/iown-homecontrol-esp32sx1276) (SX1276 register handling). The vendored/ported files live in `components/iohc/` with their original copyright headers intact; see the comment at the top of each file for what was ported near-verbatim versus rewritten for ESPHome.

### Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **868MHz** V1.6.1 specifically. Requires the `arduino` framework (not `esp-idf`, used by the RTS bridge) since the vendored radio code uses Arduino's `SPI`/`Preferences` libraries directly - both are declared under `esphome: libraries:` in `somfy-io-bridge.yaml` since PlatformIO's dependency finder doesn't pick them up automatically from a local `external_component`.

---

## OLED display

Both bridges' onboard SSD1306 cycles through a page per physical cover (name + current position) plus a shared status page (device IP, and RTS: Bluetooth proxy state; IO: packet count/RSSI), switching automatically every 3 seconds. IO's RSSI is hub-level only (last received packet, any source) - not attributable to a specific motor, since 1W motors never transmit anything back to measure RSSI of; real per-motor RSSI would need 2W's own acknowledgment traffic, which isn't implemented.
