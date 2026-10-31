# Firmware builds and the flasher

CI compiles every board on every push and publishes a firmware build for every commit on
`main`. The flasher (a Mac app in `flasher/`) downloads firmware builds, tells which one a
plugged-in core runs, and flashes it.

## Boards

| id | name | fqbn | extra flags |
|---|---|---|---|
| `atom-lite` | M5Atom Lite | `m5stack:esp32:m5stack_atom` | |
| `atom-echo` | M5Atom Echo | `m5stack:esp32:m5stack_atom` | `-DARDUINO_M5STACK_ATOM_ECHO` |
| `atoms3-lite` | M5AtomS3 Lite | `m5stack:esp32:m5stack_atoms3` | |
| `stickc-plus` | M5StickC Plus | `m5stack:esp32:m5stack_stickc_plus` | |

The table lives in `ci/build-firmware.sh`; a new board is a row there plus a branch in
`Wiring.h`.

## Firmware build layout

`ci/build-firmware.sh OUTDIR` writes one firmware build. CI publishes it as a GitHub
prerelease tagged `firmware-<7-char commit>` on nevyn/shinercore, with every file in
OUTDIR as an asset:

* `<board>-bootloader.bin`, `<board>-partitions.bin`, `<board>-boot_app0.bin`,
  `<board>-app.bin`: the flash pieces.
* `manifest.json`:

```json
{
  "schema": 1,
  "commit": "<40-char sha>",
  "date": "<commit date, ISO 8601>",
  "subject": "<commit subject>",
  "boards": [{
    "id": "atom-lite",
    "name": "M5Atom Lite",
    "chip": "esp32",
    "elfSha256": "<64 hex>",
    "flashMode": "dio", "flashFreq": "80m", "flashSize": "4MB",
    "uploadSpeed": 1500000,
    "parts": [{"offset": 4096, "file": "atom-lite-bootloader.bin", "size": 17536, "sha256": "<64 hex>"}]
  }]
}
```

Readers ignore unknown fields; a breaking change bumps `schema`.

## Build fingerprint

The core's elf2image step (`--elf-sha256-offset 0xb0` in the m5stack platform.txt) writes
the SHA-256 of the ELF into the app image header, at `esp_app_desc_t.app_elf_sha256`. The
app image sits at flash `0x10000`, so the fingerprint is the 32 bytes at `0x100b0`.
`build-firmware.sh` checks that each app image carries its ELF's SHA. The flasher reads
those bytes with `esptool read-flash 0x100b0 32` and looks them up in the manifests'
`elfSha256`. A miss means a local build or other firmware. Reading flash resets the core
into its bootloader, so its lights go dark for a few seconds.

## Flashing preserves settings

The flasher writes each part at its own offset, like `arduino-cli upload`, and never a
merged image. `esptool merge_bin` fills the gaps between pieces with `0xFF`, and NVS
(`0x9000`–`0xe000`) lies in the gap between the partition table and `boot_app0`: a merged
image erases every preset, wiring and setting. The flasher's factory reset adds
`--erase-all` to `write-flash`, which erases the whole flash first, on purpose.

## Flasher releases

Flasher releases are full (not pre-) releases tagged `flasher-v<version>`, with the
notarized `ShinerCoreFlasher.zip` attached. Firmware builds are prereleases, so
<https://github.com/nevyn/shinercore/releases/latest/download/ShinerCoreFlasher.zip>
always points at the newest flasher.
