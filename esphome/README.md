# Somfy Bridges (ESPHome)

Local, cloud-free control of Somfy covers from Home Assistant - no TaHoma/Connexoon box required for either bridge.

Both bridges used to be documented and versioned together in this repo. They're now split into their own standalone, public repos, each with its own full README, release history, and issue tracker:

- **[danielpetrovic/somfy-rts-bridge](https://github.com/danielpetrovic/somfy-rts-bridge)** - Somfy RTS (one-way, rolling code), 433MHz.
- **[danielpetrovic/somfy-io-bridge](https://github.com/danielpetrovic/somfy-io-bridge)** - Somfy IO (io-homecontrol), 868MHz. 1W stable, 2W bonding still an open problem - see that repo's README for the full investigation.

The four top-level files here (`somfy-rts-bridge.yaml`, `somfy-rts-cover.yaml`, `somfy-io-bridge.yaml`, `somfy-io-cover.yaml`) are symlinks into local clones of those two repos under `esphome/dev/` (gitignored - not real files, and not tracked by this repo), not real files - this is how the actual ESPHome dashboard on this install keeps flashing them day to day. They have to live under `/config` specifically, not `/share`: the ESPHome add-on's own container only has `/config` mounted, so a symlink pointing outside it resolves to nothing from the add-on's perspective and its dashboard silently drops the entry.

The IO bridge's `components/iohc` no longer needs a local copy at all - `somfy-io-bridge.yaml` sources it directly via `external_components: type: git`, so ESPHome fetches it straight from GitHub at compile time.

`secrets.yaml` stays local to this repo/install, unaffected by the split (see each bridge's own README for which `!secret` keys it needs).
