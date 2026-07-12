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
| Protocol | RTS (one-way, rolling code) | IO (io-homecontrol) - 1W (one-way), plus experimental 2W (two-way) |
| Frequency | 433MHz | 868MHz |
| Board | LilyGO TTGO T3 LoRa32 433MHz V1.6.1 | LilyGO TTGO T3 LoRa32 868MHz V1.6.1 |
| Status | Stable | Stable (1W) - 2W implemented but unable to bond, see below |
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

## Before you start

Both bridges are **ESPHome** projects, driven from Home Assistant's own **ESPHome app** (called an "add-on" before Home Assistant 2026.2 renamed these to "apps" - you'll still see the older name in older screenshots/guides), not a separate program you install on your PC. ESPHome itself is maintained by Nabu Casa, the same company behind Home Assistant, which is why it integrates this smoothly (auto-discovery, native API, OTA updates from within Home Assistant).

Install it in Home Assistant first: Settings → Apps → search "ESPHome" → Install → Start (turning on "Show in sidebar" is convenient). This app is what compiles the `.yaml` files described in each bridge's own Setup section below into firmware and flashes it onto the board - no PlatformIO, Arduino IDE, or separate `esphome` command-line install needed on your own computer.

---

## Somfy RTS Bridge

Drives the board's onboard SX1276 radio directly in raw OOK mode. No extra hardware required.

### Setup

1. **Hardware**: LilyGO TTGO T3 LoRa32 **433MHz** V1.6.1 - nothing else needed, the onboard radio and its pin wiring (DIO2→GPIO32) are already correct for this exact board revision.
2. **Place this bridge's files** - `somfy-rts-bridge.yaml` and `somfy-rts-cover.yaml` - under `/config/esphome/` on your Home Assistant instance (that's where this repository's own `esphome/` folder maps to). Any app that lets you browse/edit files under `/config` works for this - Studio Code Server, File Editor, or a Samba/SSH share to your PC are all fine.
3. **Secrets**: add `wifi_ssid`, `wifi_password`, `rts_bridge_api_key`, and `rts_bridge_ota_password` to `secrets.yaml` under `/config/esphome/` (shared by both bridges - if you've already set up the IO bridge, just add the two `rts_bridge_*` lines to the existing file). Two ways to get the values:

   #### Normal flow (recommended)

   Entirely inside the ESPHome app, no terminal needed:
   - Open the ESPHome app → the "⋮" (three-dot) menu, top right → **Secrets**. Edits `secrets.yaml` directly inside the app, creating it if it doesn't exist yet.
   - For `rts_bridge_api_key` (a base64-encoded 32-byte value): ESPHome's own site has a generator built into its docs, entirely client-side (nothing sent anywhere) - open [esphome.io/components/api](https://esphome.io/components/api/#configuration-variables), find the encryption key generator, and use its **Copy** button.
   - For `rts_bridge_ota_password`: no format requirement, just needs to exist - any password works, typed directly into the Secrets editor. For a random one instead of hand-picked, click **Regenerate** then **Copy** on that same generator page a second time.

   #### Advanced flow

   A terminal instead - useful if you'd rather script it. Home Assistant's own "Terminal & SSH" or "Studio Code Server" app always has `openssl` available regardless of your own computer's OS (on Windows, `openssl` usually isn't available unless you have Git for Windows - use its "Git Bash"). No terminal at all? Your browser's own Developer Tools (`F12` → Console) works too: `Array.from(crypto.getRandomValues(new Uint8Array(32)), b => b.toString(16).padStart(2, '0')).join('')` prints hex, which works fine as the base64 field's value too - it's raw random bytes either way.

   ```yaml
   wifi_ssid: "your_wifi_name"
   wifi_password: "your_wifi_password"
   rts_bridge_api_key: "base64-encoded-32-byte-key"      # openssl rand -base64 32
   rts_bridge_ota_password: "some-password"              # openssl rand -hex 16
   ```
4. **Add an area + device entry** for each physical cover under `esphome:` in `somfy-rts-bridge.yaml` - this is what makes the cover show up as its own HA device (matching Overkiz's per-shade/curtain granularity) instead of bunched onto the hub:
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
5. **Add a cover entry** for each physical cover under `packages:` in `somfy-rts-bridge.yaml` (`cover_id` + `_device` must match the device `id` above):
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
6. **Flash** `somfy-rts-bridge.yaml` via the ESPHome app - open the app, find this file's card, and click **Install**:
   - **First flash**: choose "Plug into this computer", connect the board via USB, and pick the serial port when your browser prompts for it (only works from a browser tab open on the machine physically connected to the board).
   - **Every flash after that** (updates): choose "Wirelessly" instead - no USB needed, once the board is already on your Wi-Fi.
   - After a successful first flash, the board announces itself to Home Assistant automatically - go to Settings → Devices & Services, an "ESPHome" discovery card should appear; click **Configure** and enter the `rts_bridge_api_key` value from `secrets.yaml` the one time it's needed.
7. **Pair each cover to its motor** (see below).
8. **Test**: Open/Close/Stop from the cover's card in Home Assistant.

### Pairing (and unpairing) a cover to its motor

Single `Program` button. RTS has only one PROG command (`SOMFY_PROG`, no separate "remove" value exists in the protocol at all) - per Somfy's own official instructions for adding/removing a remote, the motor decides add vs. remove based on the **order** two PROG presses arrive in, not on any command distinction. **Getting the order backwards removes the wrong control** - this is Somfy's documented procedure, not something this bridge invented or can paper over.

**To add** (pair a new cover to a motor that already has at least one working control):
1. If the already-paired remote you're using to open programming mode has multiple channels (e.g. a Telis 16), select the correct channel on it first.
2. Press and hold that remote's PROG button until the motor jogs. The motor then stays in programming mode for about **2 minutes**.
3. Within that window, press the new cover's `<Cover Name> Program` button in Home Assistant. The motor jogs again, confirming the new remote is now paired, in addition to (not instead of) any existing remote.

**To remove** (e.g. decommissioning a cover) - the two presses must happen in this exact order:
1. **First**, press and hold PROG on the control you want to **keep** (a physical remote or TaHoma - not the one being removed; select the correct channel first if it's multi-channel). Hold until the motor jogs.
2. **Then**, within the same programming window, press the `<Cover Name> Program` button in Home Assistant for the cover you want to **remove**. The motor jogs again, confirming removal.

Reversing this order removes the keeper instead of the target - always press the control you're keeping first.

> [!WARNING]
> Per Somfy's own instructions: a control can only be removed if **another** control is available to open the motor's programming window first. If this board's Program button is the *only* paired control for a motor and you remove it, you lose the ability to open that motor's programming window at all - you'd need the motor manufacturer's own hard-reset procedure to recover, see [Factory-resetting a motor](#factory-resetting-a-motor-double-power-cut) below. Always keep at least one other working control paired before removing this board's identity.

RTS has no acknowledgment channel at all. There's no way to confirm a Program press actually took beyond watching the motor jog; if it doesn't respond as expected, it likely wasn't in its programming window at the right moment, just try again.

### Factory-resetting a motor (Double Power Cut)

Documented in Somfy's own motor manuals as one option in the motor's troubleshooting flow - this section covers the mechanics of the reset itself, once you've decided it's needed, not when to reach for it over a lighter remedy. Both paths below start with the same Double Power Cut; only the second half differs depending on whether you already have a working paired remote.

**Step 1 (both paths) - Double Power Cut:**
1. Motor powered.
2. Cut power - **2-3 seconds**.
3. Restore power - **8 seconds**.
4. Cut power again - **2-3 seconds**.
5. Restore power - the motor moves for ~5 seconds (or a short up/down movement if it was sitting at an end-limit), confirming the cut registered.

> [!WARNING]
> Only power the specific motor being reset - anything else sharing the same circuit gets reset too. Avoid using TaHoma or another remote/box to control this motor during the procedure.

Timing by hand is error-prone - a smart plug driven by a short HA script gets it exact:
```yaml
script:
  reset_shutter_motor_dpc:
    alias: "Reset Shutter Motor (Double Power Cut)"
    sequence:
      - service: switch.turn_off
        target: {entity_id: switch.YOUR_SMART_PLUG}
      - delay: "00:00:02"
      - service: switch.turn_on
        target: {entity_id: switch.YOUR_SMART_PLUG}
      - delay: "00:00:08"
      - service: switch.turn_off
        target: {entity_id: switch.YOUR_SMART_PLUG}
      - delay: "00:00:02"
      - service: switch.turn_on
        target: {entity_id: switch.YOUR_SMART_PLUG}
```

**Step 2 depends on your situation:**
- **You have a working, currently-paired remote**: hold its PROG button for **more than 7 seconds**, until the motor does **2 short movements → OK**. This erases every *other* control, but the remote you used stays paired - you can go straight into calibration with it afterward without pressing PROG again.
- **You have no working remote** (lost or defective): using the **new** replacement remote you intend to pair, press PROG **briefly - under 1 second** - until the motor does **1 short movement → OK**. This erases every *other* control, and pairs the new remote immediately as part of the same step.

**What gets reset either way**: all settings (except the retraction/back-impulse function) return to factory defaults.

**After either path, before the motor is usable with this bridge**:
1. Make sure you have one working physical remote paired (per whichever path above applied) and use it to program travel limits - this bridge has no travel-limit/calibration feature of its own.
2. Check rotation direction while in calibration mode: long-press UP and DOWN together to enter it, then press and hold UP or DOWN to see which way the motor actually turns. If reversed, press **MY for 2 seconds** to flip it.
3. Only after travel limits are calibrated, add this bridge as an additional control via its own Program button (see [Pairing](#pairing-and-unpairing-a-cover-to-its-motor) above).
4. If TaHoma or another box was previously used to control this motor, re-add it through its normal app flow (same PROG-press ceremony as pairing any other remote).

### Modes

Each cover has a `select:` entity to switch between:
- **`My`** (default) - three fixed states, no time-based estimation, no drift:
  - **0%** → `DOWN` (fully closed)
  - **50%** → `MY` (the motor's own favorite/preset position, whatever that's set to)
  - **100%** → `UP` (fully open)
  - Stop always sends `MY` too, matching how a real Somfy remote's MY button behaves (stops in place if moving, jumps to the preset if idle).
- **`Timed`** - local travel-time-based position estimate (`Travel Time Open`/`Travel Time Close` number entities, per-cover, default 25s each, adjustable live from Home Assistant - no reflash needed). Approximate; can drift on repeated partial moves since RTS is one-way and the motor never reports real position back. Requesting an intermediate position sends `UP`/`DOWN` and auto-stops (`MY`) once the estimate reaches the target.

### Retrying weak-signal commands

Each cover has a **`Retry Weak Signal Commands`** switch (off by default, per cover) for shutters with marginal RTS reception - RTS is one-way with no acknowledgment, so a command can silently get dropped with no way to detect it happened. When enabled, an Open or Close command automatically resends itself once, `retry_delay` (default 3s) later - unless another command (Stop, My, a repeat Open/Close, or a partial-position move) was issued in the meantime, in which case the pending retry is cancelled instead of firing. Entirely on-device (an ESPHome `script:`, `mode: restart` handles the cancel-on-any-other-command behavior); enabling the switch just arms it, no Home Assistant automation involved. When it does fire, it logs clearly (`tag: retry`, `INFO` level, e.g. `"Hallway Shade: resending Close (retry, no other command in 3s)"`) so it's unambiguous in the logs rather than something you have to infer from timing.

Opt-in rather than a blanket default since it doubles a cover's RF traffic on every Open/Close - only worth enabling for shutters that actually show the problem (motor doesn't respond to every command).

### How it works

- `sx127x` (ESPHome core) puts the SX1276 into raw OOK mode at 433.42MHz (Somfy's exact carrier, not the 433.92MHz ISM default).
- `remote_transmitter`/`remote_receiver` (ESPHome core) bit-bang the OOK data through the shared GPIO32/DIO2 pin.
- [`swoboda1337/somfy-esphome`](https://github.com/swoboda1337/somfy-esphome) (external component) implements the actual Somfy RTS protocol (rolling codes, frame encoding) on top of that radio. Ported from [`Legion2/Somfy_Remote_Lib`](https://github.com/Legion2/Somfy_Remote_Lib).
- Each cover is a `template` cover - see [Modes](#modes) above for the two position models it can switch between.

### Files

- `somfy-rts-bridge.yaml`: the device config (radio setup, Wi-Fi/API/OTA, the OLED display, `bluetooth_proxy`, diagnostic entities (WiFi Signal, Uptime, Loop Time, Restart Reason, Restart), configuration entities (Display, Display Brightness, Display Page Interval), a Debug Logging control switch, and one `packages:` entry per physical cover).
- `somfy-rts-cover.yaml`: reusable package template (virtual remote, Program button, My button, Mode select, Travel Time Open/Close numbers, Retry Weak Signal Commands switch, and the cover logic above), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `remote_address`, `device_class`, `travel_time_open`, `travel_time_close`, `retry_delay` - see [Retrying weak-signal commands](#retrying-weak-signal-commands)).

### Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **433MHz** V1.6.1 specifically. Pin numbers throughout (`spi:`, `sx127x:`, `i2c:`) are for that exact board revision, check your board's pinout before reusing this on anything else.

---

## Somfy IO Bridge

Drives the board's onboard SX1276 radio directly at the register level (no ESPHome core radio component covers IO's (io-homecontrol's) FSK framing), via a vendored port of a real working implementation rather than a from-scratch reimplementation.

### Setup

1. **Hardware**: LilyGO TTGO T3 LoRa32 **868MHz** V1.6.1 - nothing else needed. Pin mapping (confirmed against the official schematic) lives in `components/iohc/iohc_board_config.h`.
2. **Place this bridge's files** - `somfy-io-bridge.yaml`, `somfy-io-cover.yaml`, and `components/iohc/` - under `/config/esphome/` on your Home Assistant instance (that's where this repository's own `esphome/` folder maps to). Any app that lets you browse/edit files under `/config` works for this - Studio Code Server, File Editor, or a Samba/SSH share to your PC are all fine.
3. **Secrets**: add `wifi_ssid`, `wifi_password`, `io_bridge_api_key`, and `io_bridge_ota_password` to `secrets.yaml` under `/config/esphome/` (shared by both bridges - if you've already set up the RTS bridge, just add the two `io_bridge_*` lines to the existing file; each bridge needs its own separate key/password, don't reuse the RTS bridge's). Two ways to get the values:

   #### Normal flow (recommended)

   Entirely inside the ESPHome app, no terminal needed:
   - Open the ESPHome app → the "⋮" (three-dot) menu, top right → **Secrets**. Edits `secrets.yaml` directly inside the app, creating it if it doesn't exist yet.
   - For `io_bridge_api_key` (a base64-encoded 32-byte value): ESPHome's own site has a generator built into its docs, entirely client-side (nothing sent anywhere) - open [esphome.io/components/api](https://esphome.io/components/api/#configuration-variables), find the encryption key generator, and use its **Copy** button.
   - For `io_bridge_ota_password`: no format requirement, just needs to exist - any password works, typed directly into the Secrets editor. For a random one instead of hand-picked, click **Regenerate** then **Copy** on that same generator page a second time.
   - This flow **doesn't** cover the optional fixed `node`/`key` device identity in step 4 below - that's not an ESPHome-recognized value, so it has no dedicated generator page; it always needs the Advanced flow.

   #### Advanced flow

   A terminal instead - useful if you'd rather script it, or need step 4's optional `node`/`key` values. Home Assistant's own "Terminal & SSH" or "Studio Code Server" app always has both `openssl` and Python 3 available regardless of your own computer's OS (on Windows, neither is usually available unless you have Git for Windows - use its "Git Bash"). No terminal at all? Your browser's own Developer Tools (`F12` → Console) works too: `Array.from(crypto.getRandomValues(new Uint8Array(32)), b => b.toString(16).padStart(2, '0')).join('')` prints hex, which works fine as the base64 field's value too - it's raw random bytes either way.

   ```yaml
   wifi_ssid: "your_wifi_name"
   wifi_password: "your_wifi_password"
   io_bridge_api_key: "base64-encoded-32-byte-key"     # openssl rand -base64 32
   io_bridge_ota_password: "some-password"             # openssl rand -hex 16
   ```
4. **(Optional) Generate a fixed identity for the new cover** - via the Advanced flow above (see step 3), since this value has no ESPHome-provided generator. Not required - leave `node`/`key` as empty strings (the default) and the board generates a random identity itself on first boot, which works completely fine. The only reason to do this step is so that identity **survives a board replacement**: with the default random identity, a replacement board generates a *different* one and every motor needs re-pairing; with a fixed identity from `secrets.yaml`, a replacement board flashed with the same config reproduces the exact same identity, no re-pairing needed. Worth doing *before* pairing a new cover if you want this, since switching an already-paired cover from random to fixed later needs an unpair/re-pair cycle anyway - see [Board-independent identity](#board-independent-identity-surviving-a-board-replacement) below for the command.
5. **Add a cover entry** for each physical cover under `packages:` in `somfy-io-bridge.yaml`, including the `node`/`key` from step 4 if you generated them (otherwise leave both as empty strings):
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
   This creates the cover plus its "Program" button (pairs and unpairs, like the physical remote's own PROG button), a "My" button, three Identify buttons (see [Identify](#identify)), and "Mode" select. See `somfy-io-cover.yaml` for the full set of optional vars (`device_class`, defaults to `shutter` - see [Cover device classes](#cover-device-classes); `broadcast_type` - rarely needs overriding). `node`/`key` can be left as empty strings instead, in which case a random identity is auto-generated and stored on the board's own flash on first boot - not recommended, see step 4.
6. **Flash** `somfy-io-bridge.yaml` via the ESPHome app - open the app, find this file's card, and click **Install**:
   - **First flash**: choose "Plug into this computer", connect the board via USB, and pick the serial port when your browser prompts for it (only works from a browser tab open on the machine physically connected to the board).
   - **Every flash after that** (updates): choose "Wirelessly" instead - no USB needed, once the board is already on your Wi-Fi.
   - After a successful first flash, the board announces itself to Home Assistant automatically - go to Settings → Devices & Services, an "ESPHome" discovery card should appear; click **Configure** and enter the `io_bridge_api_key` value from `secrets.yaml` the one time it's needed.
7. **Pair each cover to its motor** (see below).
8. **Test**: Open/Close/Stop from the cover's card in Home Assistant. The mode `select` defaults to `Position`, which supports real percentage targets - see [Modes](#modes).

### Pairing (and unpairing) a cover to its motor

Single `Program` button. `Add` (cmd `0x30`) and `Remove` (cmd `0x39`) are two structurally distinct commands - checked directly against upstream's own reference implementation (`rspaargaren/iohomecontrol`) - and the Program button picks between them by reading this bridge's own persisted pairing record (was the last successful Add/Remove for this cover an add or a remove?) and sending the matching command.

**To add** (pair a new cover to a motor that already has at least one working control):
1. If the already-paired remote you're using to open programming mode has multiple channels, select the correct channel on it first.
2. Press and hold that remote's PROG button until the motor jogs (can take a few seconds, don't give up too early). The motor then stays in programming mode for about **10 minutes** (confirmed via a real capture - the motor jogs a second time, unprompted, when the window closes on its own; no RF traffic accompanies that closing jog, it's a purely local/mechanical indication).
3. Within that window, press the new cover's `<Cover Name> Program` button in Home Assistant (transmits `Add`, cmd `0x30` - the actual key-exchange command; despite the label, this is *not* the same as `RemoteButton::Pair`/cmd `0x2e`, which only applies once a key is already shared). The motor jogs again, confirming the change - the new virtual remote is now bonded, in addition to (not instead of) any existing remote (e.g. TaHoma, or a physical Situo).

**To remove** (e.g. before migrating to a fixed node/key, or decommissioning a cover) - the two presses in this order:
1. **First**, press and hold PROG on the control you want to **keep** (a physical remote or TaHoma - not the cover being removed; select the correct channel first if it's multi-channel). Hold until the motor jogs.
2. **Then**, within the same programming window, press the `<Cover Name> Program` button in Home Assistant for the cover you want to **remove** (transmits `Remove`, cmd `0x39`, per this bridge's own persisted record).

> [!WARNING]
> Per Somfy's own instructions: a control can only be removed if **another** control is available to open the motor's programming window first. If this board's Program button is the *only* paired control for a motor and you remove it, you lose the ability to open that motor's programming window at all (nothing left to press PROG on) - you'd need the motor manufacturer's own hard-reset procedure to recover, see [Factory-resetting a motor](#factory-resetting-a-motor-double-power-cut-1) below. Always keep at least one other working control (TaHoma, a physical Situo/Telis, etc.) paired before removing this board's identity.

There's no acknowledgment in this protocol, so this bridge's record of whether a cover is currently bonded is an assumption, not a verified fact - if an Add/Remove is sent while the motor wasn't actually in its programming window, that record and the motor's real state fall out of sync (the frame goes nowhere, but the record still updates as if it worked). Confirming the motor stops responding (after a removal) or starts responding (after an add) is the only way to verify a press actually took; if it doesn't match what you expected, the Program button may need pressing twice to get back in sync.

### Factory-resetting a motor (Double Power Cut)

Documented in Somfy's own motor manuals as one option in the motor's troubleshooting flow - this applies to both RTS and IO motors. This section covers the mechanics of the reset itself, once you've decided it's needed, not when to reach for it over a lighter remedy. Both paths below start with the same Double Power Cut; only the second half differs depending on whether you already have a working paired remote.

**Step 1 (both paths) - Double Power Cut:**
1. Motor powered.
2. Cut power - **2-3 seconds** (the manual's own diagram shows a 2s minimum; TaHoma's own "advanced reset" flow uses 3s, and 3-8-3 timing has been confirmed working in practice).
3. Restore power - **8 seconds**.
4. Cut power again - **2-3 seconds**.
5. Restore power - the motor moves for ~5 seconds (or a short up/down movement if it was sitting at an end-limit), confirming the cut registered.

> [!WARNING]
> Per the manual: only power the specific motor being reset - anything else sharing the same circuit gets reset too. Do not use a 2-way remote/box (TaHoma, Connexoon) during this procedure.

Timing by hand is error-prone - a smart plug driven by a short HA script gets it exact:
```yaml
script:
  reset_shutter_motor_dpc:
    alias: "Reset Shutter Motor (Double Power Cut)"
    sequence:
      - service: switch.turn_off
        target: {entity_id: switch.YOUR_SMART_PLUG}
      - delay: "00:00:02"
      - service: switch.turn_on
        target: {entity_id: switch.YOUR_SMART_PLUG}
      - delay: "00:00:08"
      - service: switch.turn_off
        target: {entity_id: switch.YOUR_SMART_PLUG}
      - delay: "00:00:02"
      - service: switch.turn_on
        target: {entity_id: switch.YOUR_SMART_PLUG}
```

**Step 2 depends on your situation:**
- **You have a working, currently-paired remote**: hold its PROG button for **more than 7 seconds**, until the motor does **2 short movements → OK**. This erases every *other* 1-way control, but the remote you used stays paired - you can go straight into calibration with it afterward without pressing PROG again.
- **You have no working remote** (lost or defective): using the **new** replacement remote you intend to pair, press PROG **briefly - under 1 second** - until the motor does **1 short movement → OK**. This erases every *other* 1-way control, and pairs the new remote immediately as part of the same step.

**What gets reset either way**: all settings (except the retraction/back-impulse function) return to factory defaults. **What's genuinely unclear**: whether an existing 2-way bond (TaHoma, Connexoon) survives - the manual doesn't say either way, and it can't be tested in isolation in practice (post-reset calibration and any 2-way box's own onboarding both require a fresh 1-way pairing ceremony, which confounds any attempt to observe 2-way state on its own).

**After either path, before the motor is usable with this bridge**:
1. Make sure you have one working physical remote paired (per whichever path above applied) and use it to program travel limits - this bridge has no travel-limit/calibration feature of its own.
2. Check rotation direction while in calibration mode: long-press UP and DOWN together to enter it, then press and hold UP or DOWN to see which way the motor actually turns. If reversed, press **MY for 2 seconds** to flip it.
3. Only after travel limits are calibrated, add this bridge as an additional control via its own Program button (see [Pairing](#pairing-and-unpairing-a-cover-to-its-motor-1) above).
4. If a 2-way box was previously bonded, re-add the device through its normal app flow - it will ask for a PROG press on a paired 1-way remote as part of its own search.

### Identify

Three extra buttons per cover - `<Cover Name> Identify`, `Start Identify`, `Stop Identify` - jog the motor without changing its position, useful for visually confirming which physical motor a Home Assistant entity actually maps to. `Identify` is a one-shot jog (matches TaHoma's own "locate device" action); `Start`/`Stop Identify` are for a manual continuous blink. Confirmed working against real hardware. Always transmits over 1W, regardless of the cover's own `Mode` selection - unlike the cover's own Open/Close/Stop/Position controls, Identify was never wired to route through 2W.

### Get Name (2W)

An opt-in diagnostic button, `<Cover Name> Get Name (2W)`, separate from bonding itself - both this project's own reference implementations treat device naming as out of scope for the pairing sequence proper, so this follows the same separation. Sends `GET_NAME` (`0x50`) to this cover's motor - a real reply (`0x51`, 16-byte ASCII name) is strong end-to-end confirmation the *entire* crypto chain (derived `system_key`, live challenge/response) actually works, not just that the discovery/bonding handshake succeeded. Requires this motor to already be 2W-bonded via `Program (2W)` first - refuses otherwise, same as any other 2W command. On a successful bond, the motor's name is also automatically set (`SET_NAME`, `0x52`) to match this cover's own Home Assistant name - no separate button for that half, it just happens once as part of `Program (2W)` completing.

No physical Somfy remote has an Identify button, so there was no confirmed 1W frame format to copy going in - what's implemented is a best-guess signed 1W frame built by matching the command byte (`cmd=0x1E`) and payload bytes seen in a real captured 2W TaHoma-to-motor exchange, wrapped in the same self-signed `data+sequence+hmac` structure this bridge's own Pair/Remove frames use (since a 1W broadcast has no live challenge/response round trip to lean on the way 2W does). That guess turned out correct - it works over 1W despite Identify itself only being documented/observed as a 2W (TaHoma) feature.

### Board-independent identity (surviving a board replacement)

By default, each cover generates a **random** virtual remote identity (a 3-byte node address + 16-byte AES key) on first boot, stored only in that specific board's flash (NVS). If the board ever dies, a replacement would generate a *different* random identity, requiring every motor to be re-paired.

Do this **before** pairing a new cover, not after - switching an already-paired cover from random to fixed needs an unpair/re-pair cycle (see step 3 in Setup above). Generate a fixed identity for the cover - this is a one-off, offline step that just produces two random hex strings, it does **not** run on Home Assistant, the ESPHome app, or the board itself. Run it via the Advanced flow from step 3 in Setup above (Home Assistant's Terminal & SSH/Studio Code Server, your own computer's terminal, or the browser console fallback):
```shell
python3 -c "import secrets; print('node:', secrets.token_hex(3)); print('key:', secrets.token_hex(16))"
```
No Python available where you're running this? Two separate `openssl` calls work just as well: `openssl rand -hex 3` (node) and `openssl rand -hex 16` (key).

Then add the printed values to `secrets.yaml`, named after the cover:
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

**The same idea applies to this bridge's own 2W controller identity** (the box/TaHoma-like role, see [Modes](#modes) below) - one shared identity for the whole bridge, not per-cover, set on the `iohc:` hub itself rather than per-cover:
```yaml
iohc:
  id: iohc_hub
  controller_address: !secret io_bridge_2w_controller_address
  system_key: !secret io_bridge_2w_system_key
```
Generated the same way (`secrets.token_hex(3)`/`secrets.token_hex(16)`, or the two `openssl rand -hex` calls above), stored under whatever names you like in `secrets.yaml`. Leave both unset (the default) to keep the random-generate-and-persist-on-device behavior. As of this writing no real 2W bond has ever succeeded on any install this bridge has been tested against (see [Modes](#modes) below), so there is nothing to lose by fixing this now rather than after a real bond exists.

### Modes

Each cover has a `select:` entity to switch between:
- **`Position`** (default) - the only mode that sends arbitrary percentage targets to the motor. The motor itself executes these accurately (confirmed via passive 2W read-back across multiple physical devices/percentages); the displayed position while a move is in progress is a fixed 25s travel-time animation, purely cosmetic (not user-configurable - there's nothing to tune since it never affects where the motor actually ends up, only how long the UI shows "still moving").
- **`Open / My / Close`** - three discrete states (0% closed / 50% "My" / 100% open), no time tracking, no arbitrary percentages - any position request strictly between 0 and 100 collapses to `Vent` (`main=0xd8`), the real My/favorite-position command, confirmed via a live capture of a real TaHoma "My" press. My and Stop are **not** the same command: the dedicated Stop action still sends `Stop` (`main=0xd2`), which (also confirmed via live capture) only has an effect while the motor is actively moving.
- **`Two-Way (Experimental)`** - real 2W commands to an actually-bonded motor. The control path (per-command challenge/response, position feedback) is implemented and validated against real captured frames. This bridge's own bonding has never yet succeeded against real hardware, though - see [2W bonding: current status and open problem](#2w-bonding-current-status-and-open-problem) below for the full picture. Selecting this mode on an unbonded motor just logs a warning and ignores commands.

### Real position feedback (passive 2W decode)

Independent of the Mode above, a cover with `motor_address` set (see `somfy-io-cover.yaml`) gets a standalone **Target Closure** sensor populated from the real motor whenever an *existing* 2W-bonded controller (TaHoma, Connexoon, or any other io-homecontrol box) talks to it - even though this bridge never initiated that exchange.

**This only updates the Target Closure sensor - it deliberately never touches the cover entity's own position/state.** An earlier version of this feature also overwrote the cover's own position, which sounded right (real data should beat a guess) but backfired: passively decoded frames aren't reliably validated yet (see the integrity caveat below), and a bad decode was confirmed corrupting the actual cover entity used for real control. The sensor is an explicit best-effort side-channel for monitoring/automations; the cover's own displayed position only ever comes from commands this bridge itself sent and tracked.

**How it works:** io-homecontrol's 2W protocol runs a real AES challenge/response handshake for authentication, but the command/status *data* inside those frames is plaintext, not encrypted - confirmed by decoding it without ever knowing the key TaHoma and the motor share. Whenever TaHoma sends a movement command (`CMD 0x00`) and the motor answers once it's physically settled (`CMD 0x04`, after the challenge/response completes), this bridge - already listening continuously on the same channel for its own purposes - decodes the real position directly: bytes 2-3 of the `CMD 0x04` answer carry the same Main Parameter value the movement command itself used, on a `0x0000`-`0xC800` = `0-100%` scale (closure %, 0=open/100=closed - the same convention as HA's Overkiz "Target closure" sensor, so the two are directly comparable). Confirmed byte-for-byte against two different physical motors across every value from 0-100%.

**What this requires:**
- `motor_address` set to the shutter's **real** Somfy address (6 hex chars) - completely different from `node`/`key`, which are this bridge's own locally-generated 1W identity. Look it up via an existing 2W controller's device registry - e.g. Home Assistant's Overkiz integration: find the cover's `unique_id` (`io://<gateway-id>/<decimal-id>`), convert the decimal id to hex. That's `motor_address`.
- **A real, already-2W-bonded controller (TaHoma, Connexoon, or similar) actively using the shutter.** This bridge only *overhears* that traffic - it never initiates a query of its own, and can't: sending an unbonded 2W query to a motor with no prior bonding was tested directly and got zero response (no challenge, no answer, nothing at all). 1W pairing does not grant any standing on a motor's completely separate 2W trust system.
- **If you only ever control your shutters over 1W** (a physical remote, or this bridge alone, with no TaHoma/Connexoon/similar box ever bonded to them), **this feature will not produce anything.** Whether real position is obtainable at all in a pure-1W-only setup is genuinely unconfirmed - it's never been tested, since testing this bridge has always had a TaHoma alongside it. If that's your setup and you try this, please report back what you see (or don't see) so this note can be corrected either way.
- Freshness depends entirely on how often the box you already have polls or moves the shutter - not continuous, not on this bridge's own schedule. In testing, the interval after a move started around 4-5s and backed off to roughly 20s at rest; the steady-state interval for a shutter that's never touched at all hasn't been fully characterized yet.

**Known limitation - no verified frame integrity check.** A garbled/corrupted reception was directly observed producing a wrong Target Closure value (decoded position contradicted the real, independently-confirmed Overkiz state at the time) - no CRC or other integrity check has been located anywhere in the RX pipeline before a frame's bytes are trusted. A basic sanity check (reject an all-zero source address) was added, but this is a partial mitigation, not a real fix - a garbled frame that happens to still carry a real registered motor's address would not be caught. Treat this sensor as best-effort, not a guaranteed-correct source of truth, until this is properly investigated.

**What this is not:** this bridge still cannot query position on its own, still cannot control shutters over 2W, and has no 2W bonding of its own with any motor. That would be a separate, much larger effort (the real AES challenge/response + key-transfer bonding ceremony, then this bridge doing its own periodic polling, independent of TaHoma).

### 2W bonding: current status and open problem

This bridge's own 2W bonding (`Program (2W)` - becoming a real independent controller for a motor, rather than just overhearing an existing box's traffic) has never yet succeeded against real hardware, despite substantial engineering effort across many sessions. This section documents what's been built, what's been ruled out, and why the underlying obstacle is still unresolved.

**What's implemented**: a full bonding state machine (`iohc_controller2w.*`) that actively broadcasts `DISCOVER` (`0x28`), and on a response, drives the real key-exchange sequence (`KEY_INIT`/`0x31` → `CHALLENGE_REQUEST`/`0x3C` → `KEY_TRANSFER`/`0x32` → `CHALLENGE_ANSWER`/`0x3D`), plus a required `DISCOVER_CONFIRMATION` (`0x2C`→`0x2D`) step. The frame construction and crypto (AES-ECB IV/challenge-response) were independently verified byte-for-byte against two separate, real working implementations - [`laberning/home_io_control`](https://github.com/laberning/home_io_control) and [`nicolas5000/io-rts-esp32`](https://github.com/nicolas5000/io-rts-esp32) - not just against upstream's own more exploratory reference code. Listen-Before-Talk RF collision avoidance and a fast (~900µs/channel) 3-channel RX hop were also added, matching those references' own documented behavior. A passive key-sniffer (watching for `0x31`→`0x3C`→`0x32` from any source, not just this bridge's own attempts) was built to try to recover an already-established key from an existing box's own bonding traffic instead.

**The obstacle**: every real bonding attempt against real hardware - including tests with an independently-confirmed-open pairing window, correct channel, working collision avoidance, and byte-identical frames to a genuine TaHoma broadcast captured on the same install - gets zero response to the initial `DISCOVER` broadcast.

**This isn't unique to this project.** Both reference implementations document the same real-world requirement: a device already bonded to another box (TaHoma, Connexoon, etc.) will not answer a new controller's discovery/bonding attempt while that bond exists. `nicolas5000/io-rts-esp32`'s own README states this explicitly as the intended workflow for its own key-extraction feature - remove the device from the box's app, **fully reset the device** (forgetting all previous pairings), re-pair a physical remote, *then* re-pair the box - only that genuine, fresh re-pairing event produces a capturable key transfer. `laberning/home_io_control`'s own config also expects to be handed an existing hub's Node ID/System Key directly if a device was previously paired elsewhere, rather than attempting to derive it independently.

**What this project has tried, following that same recipe, without success**: removing a shutter from TaHoma, a genuine factory reset (Double Power Cut - see [Factory-resetting a motor](#factory-resetting-a-motor-double-power-cut-1) above), re-pairing a 1-way remote, and re-adding the device through TaHoma's own app - the same sequence the reference project documents as sufficient. TaHoma **has** successfully re-bonded to a freshly-reset motor this way, confirmed multiple times, but the actual key-transfer exchange (`0x31`/`0x32`/`0x33`) was never captured in any of those attempts, despite the passive key-sniffer running throughout. Whether that's a timing/channel gap in the sniffer, or something about this specific motor generation/firmware that differs from what the reference authors tested against, is unresolved.

**Bottom line**: this bridge can only reliably control a motor over 1W, and can only read real position by passively decoding an *existing* box's already-bonded 2W traffic (see [Real position feedback](#real-position-feedback-passive-2w-decode) above). Becoming a real independent 2W controller - bonding with a motor itself, without relying on TaHoma/Connexoon at all - remains unsolved.

### Last RSSI sensor (this bridge's own signal strength)

Independent of both the Mode and the Target Closure sensor above, a cover with `motor_address` set also gets a standalone **Last RSSI** sensor: this bridge's own radio's signal strength (dBm) for the most recent frame received from that motor's address - any frame, 1W or 2W, decoded or not.

This is deliberately distinct from Home Assistant's Overkiz integration's own "RSSI Level"/"Discrete RSSI Level" sensors, which reflect **TaHoma's** radio, not this board's - the two can (and do) read differently depending on where each device physically sits relative to the shutter. Useful for judging this bridge's own real link quality to a specific motor (e.g. a battery/solar shutter mounted several meters up, outside) independent of how well TaHoma itself hears it.

Same requirements as Target Closure apply here too, for the same underlying reason: this only fires on a frame whose *source* address is the motor's own real address, and per everything documented in `io-2w-protocol.md`, a motor only ever transmits as part of an already-bonded 2W exchange (this bridge's own 1W commands are one-way, box-to-motor only - the motor never talks back over 1W, so they never touch this sensor). In practice this means an existing 2W-bonded controller (TaHoma, Connexoon, etc.) still needs to be actively using the shutter. The difference from Target Closure is just which frames count: any 2W frame type from that motor (discovery response, challenge, command answer, ...), not only `CMD 0x04` - so it can update slightly more often.

### Broadcast type caveat

`broadcast_type` (per cover, default `0` = "All") controls which device-class group the `Add`/`Pair` broadcast targets - see `sDevicesType` in `components/iohc/iohc_utils.h` for the full list. The default is confirmed working against real shutter motors; there should be no need to change it.

### Scope and status

**Implemented (1W, one-way commands):**
- Real-time passive reception/decode of IO traffic (RSSI per source address, logged).
- Pairing and unpairing via a single `Program` button, dispatching to `Add` (cmd `0x30`) or `Remove` (cmd `0x39`) based on this bridge's own persisted pairing status - matches how real Somfy remotes and Somfy's own official add/remove instructions actually work, see [Pairing](#pairing-and-unpairing-a-cover-to-its-motor) above.
- Open / Close / Stop / My / absolute Position (0-100%) commands, confirmed working against real hardware. My and Stop are distinct commands (`Vent`/`main=0xd8` vs `Stop`/`main=0xd2`), confirmed via live captures of real TaHoma traffic.
- Identify / Start Identify / Stop Identify (`cmd=0x1E`) - best-guess frames, confirmed working against real hardware, see [Identify](#identify) above.
- Per-cover selectable mode (`Position` / `Open / My / Close` / `Two-Way (Experimental)` - control path implemented, but this bridge's own 2W bonding has not yet succeeded against real hardware, see [Modes](#modes) above).
- **Real position feedback, passively decoded from an existing 2W controller's own traffic** (TaHoma, Connexoon, etc.) - confirmed byte-for-byte accurate against real hardware, see [Real position feedback](#real-position-feedback-passive-2w-decode) above for how it works and what it requires (notably: an existing 2W-bonded box, this bridge cannot do this alone).
- **Last RSSI sensor**, this bridge's own per-motor signal strength - see [Last RSSI sensor](#last-rssi-sensor-this-bridges-own-signal-strength) above.

All of the above confirmed working against real motors, including pairing/unpairing multiple physical shutters end-to-end.

**Implemented but not yet working (2W bonding):**
- **This bridge's own 2W bonding and control.** The real AES challenge/response + key-transfer bonding ceremony, and per-command 2W control, are both implemented and byte-verified against two independent working reference implementations - but bonding has never yet succeeded against real hardware, and it's the only path to real position feedback for an installation with no TaHoma/Connexoon/similar box at all. See [2W bonding: current status and open problem](#2w-bonding-current-status-and-open-problem) above for the full picture. Until it works, this bridge can only passively decode another controller's already-bonded 2W traffic (see [Real position feedback](#real-position-feedback-passive-2w-decode) above).

### Files

- `somfy-io-bridge.yaml`: the device config (radio setup, Wi-Fi/API/OTA, OLED display, diagnostic entities (WiFi Signal, Uptime, Loop Time, Restart Reason, Restart), configuration entities (Display, Display Brightness, Display Page Interval), Debug Logging / Channel Hop (2W) control switches, and one `packages:` entry per physical cover).
- `somfy-io-cover.yaml`: reusable package template (cover + Program button + My button + Identify/Start/Stop Identify buttons + Get Name (2W) button + Mode select + Target Closure sensor + Last RSSI sensor), instantiated per cover via substitution variables (`cover_id`, `cover_name`, `device_class`, `node`, `key`, `broadcast_type`, `motor_address` - see [Real position feedback](#real-position-feedback-passive-2w-decode)).
- `components/iohc/`: the local `external_component`.
  - Flat directory (no subdirectories except `cover/`, `button/`, `select/`, `sensor/` - the only structure ESPHome's local-component loader actually supports) - see the comment in `iohc.h` for why.
  - `iohcRadio.*`, `iohcPacket.*`, `SX1276Helpers.*`, `sx1276Regs-Fsk.h`, `TickerUsESP32.*`, `Delegate.h`: vendored radio/protocol layer, near-verbatim from upstream.
  - `iohc_remote1w.*`: the command/pairing layer (Add/Remove/Open/Close/Stop/Vent/Position/Identify), rewritten around ESPHome's `Preferences`-backed persistence instead of upstream's JSON-file + MQTT model.
  - `iohc_blind_position.*`: the local travel-time position estimator, fixed 25s open/close, used only for the cosmetic "still moving" animation in `Position` mode (see [Modes](#modes)).
  - `cover/`, `button/`, `select/`, `sensor/`: the ESPHome platform integration - `sensor/` provides both the standalone Target Closure sensor (see [Real position feedback](#real-position-feedback-passive-2w-decode)) and the Last RSSI sensor (see [Last RSSI sensor](#last-rssi-sensor-this-bridges-own-signal-strength)), selected via `type:`.

### Board-specific notes

Tested and working on the LilyGO TTGO T3 LoRa32 **868MHz** V1.6.1 specifically. Requires the `arduino` framework (not `esp-idf`) since the vendored radio code uses Arduino's `SPI`/`Preferences` libraries directly - both are declared under `esphome: libraries:` in `somfy-io-bridge.yaml` since PlatformIO's dependency finder doesn't pick them up automatically from a local `external_component`.

---

## OLED display

Both bridges' onboard SSD1306 cycles through a page per physical cover (name + current position) plus a shared status page (device IP, and RTS: Bluetooth proxy state; IO: packet count/RSSI), switching automatically every 3 seconds. IO's OLED status page shows hub-level RSSI only (last received packet, any source) - it does not cycle per-motor readings. Per-motor RSSI **is** available, just not on the OLED: see the [Last RSSI sensor](#last-rssi-sensor-this-bridges-own-signal-strength) above, exposed as a proper HA sensor per cover instead.

---

## Credits & Attribution

Neither bridge is a from-scratch reimplementation - both are built on other people's protocol reverse-engineering and radio-level work, adapted into ESPHome components.

### Somfy RTS Bridge

- [`swoboda1337/somfy-esphome`](https://github.com/swoboda1337/somfy-esphome) (external component) implements the actual Somfy RTS protocol (rolling codes, frame encoding) on top of ESPHome's own core `sx127x`/`remote_transmitter`/`remote_receiver` components.
- That component is itself ported from [`Legion2/Somfy_Remote_Lib`](https://github.com/Legion2/Somfy_Remote_Lib).

### Somfy IO Bridge

- Built on [`rspaargaren/iohomecontrol`](https://github.com/rspaargaren/iohomecontrol) (Apache-2.0) - a standalone Arduino/PlatformIO firmware for the same SX1276 radio chip, with real working pairing, 1W/2W commands, and AES/CRC handling.
- That project itself builds on [`Velocet/iown-homecontrol`](https://github.com/Velocet/iown-homecontrol) (protocol documentation and reverse-engineering, largely reconstructed from decompiled TaHoma/Cozytouch firmware) and [`cridp/iown-homecontrol-esp32sx1276`](https://github.com/cridp/iown-homecontrol-esp32sx1276) (SX1276 register handling for this exact radio chip).
- The bridge's own 2W (two-way) bonding/control logic (`iohc_controller2w.h`/`.cpp`) was corrected against [`laberning/home_io_control`](https://github.com/laberning/home_io_control) (Apache-2.0) - a mature, independently-tested ESPHome IO-Homecontrol component with confirmed real 2W pairing on the same SX1276 chip family. Its documented protocol sequence (controller-initiated discovery/key-exchange, and the two-key system_key/transfer_key model) corrected a wrong directionality assumption and a crypto bug this project's own earlier bonding attempts carried over from `rspaargaren`'s more exploratory reference code - no code was copied verbatim, only the confirmed protocol/algorithm understanding.
- Also cross-checked against [`nicolas5000/io-rts-esp32`](https://github.com/nicolas5000/io-rts-esp32) - a second, independent working implementation, used to add the required `DISCOVER_CONFIRMATION` (`0x2C`→`0x2D`) bonding step neither this project nor `laberning/home_io_control` had, add Listen-Before-Talk RF collision avoidance, and fix a second crypto bug in the key-transfer IV construction. Again, no code copied verbatim - only the confirmed protocol/algorithm details.
- The vendored/ported files live in `components/iohc/` with their original copyright headers intact - see the comment at the top of each file for what was ported near-verbatim versus rewritten for ESPHome (also summarized under [Files](#files-1) above).

### How this repository's IO bridge differs from rspaargaren/iohomecontrol directly

If you've come across [rspaargaren/iohomecontrol](https://github.com/rspaargaren/iohomecontrol) separately and are wondering whether it's just a different way to install the same thing: it isn't. The two are genuinely different projects to install and run day-to-day, even though one is built on the other's protocol work:

| | rspaargaren/iohomecontrol | This repository's IO bridge |
|---|---|---|
| What it is | Standalone Arduino/PlatformIO firmware | ESPHome `external_component` |
| How you install/flash it | Compile and flash yourself (Arduino IDE or PlatformIO) | Flash via Home Assistant's own ESPHome app - see [Before you start](#before-you-start) |
| How it talks to Home Assistant | MQTT (with MQTT discovery) | ESPHome's native API (auto-discovered, encrypted, no MQTT broker needed) |
| Radio/protocol layer | Original implementation | Ported near-verbatim from this project |
| Scope | Broader - both 1W and 2W, OLED, web server, its own pairing UI | 1W only so far - see [Scope and status](#scope-and-status) |

Only the protocol/radio-level pieces were ported (packet framing, CRC/AES handling, position estimation); the MQTT layer, web server, and Arduino `main.cpp` structure were not - this repository's bridge runs entirely inside ESPHome's own component/lifecycle model instead.
