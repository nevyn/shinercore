# Mesh sync over ESP-NOW

Device-to-device sync: connectionless 250-byte broadcasts on wifi channel 1, no
pairing, ESP32 and ESP32-S3 interoperate, coexisting with BLE on the shared radio via
IDF's arbiter. BLE device-to-device was abandoned: ESP32 BLE can barely host one
central connection while being a peripheral. Radio and protocol live in Mesh.h; the
pure decision math and wire formats in MeshLogic.h (host-tested in test/meshtest.cpp).

Everything is broadcast, versioned (`kMeshVersion`), idempotent, and must survive
lost frames. Cores that disagree on protocol version ignore each other's frames.

## Frames

- **BEAT**, 1Hz with jitter: the sender's beat grid (period, phase, beatTime,
  confidence), flags (has mic / grid confident / is relaying), and its layer 0 color
  (carried for future color-merging features).
- **PRESET**: every non-Nothing layer of the sender's current preset, one frame.
  Sent when the preset has changed and settled for 1s, plus a 5s refresh.
  Animation/blend travel as indices, only meaningful same-version. Decoded with
  hostile-input clamps: anyone can broadcast ESP-NOW.

## Beat grid sync

The follower feeds the leader's grid into the existing beat PLL
(`BeatDetector::applyNetworkBeat`): period slews, phase corrects via wrapped error
(mirrored into beatTime, like onset corrections), and the whole-beat count is adopted
so beat-synced cycles agree on which beat is beat zero. The layers' chase clamp turns
any correction into a glide — the no-jumps invariant holds across the network.

Leadership is implicit, computed per received beacon (`meshOutranks`): confidence
with a ±0.1 hysteresis band, then own-grid-over-relay, then mic-over-none, then
lowest MAC. Followers rebroadcast the grid at 0.9× the leader's confidence, so sync
propagates beyond the leader's radio range with decaying authority; the relay flag
losing rank ties makes follow-cycles impossible (test-covered). A silent leader ages
out after 5s and everyone freewheels on their own grid, still together, until
someone confident reappears.

## Preset carousel

With `meshShow` on, every core plays every core's current preset: slots =
{own + neighbors'} presets sorted by MAC, `carouselBeats` beats each. The slot number
is `floor(beatTime / carouselBeats) mod slots` — derived from the shared grid, so
synced cores show the same preset and switch on the same downbeat, zero
coordination. Each layer crosses over at its own deterministically-random point in
the slot's first 2 beats (same on every core), overlapping into a tween as
renderedLayers slew to the new targets.

localPrefs stays canonical: app edits land there, broadcast after settling, and walk
on stage when your own slot comes around. Solo (no neighbors), the carousel
degenerates to playing your own preset — behavior unchanged.

Lockstep is only as tight as the neighbor tables: cores at radio-range edges hear
different sets, and a join/leave remaps everyone's slot list mid-slot. The settings
slew keeps those moments graceful rather than correct.

## Settings

| property | default | meaning |
|---|---|---|
| `mesh` | 1 | the radio: beacons, following, preset sharing. Battery kill switch. |
| `meshShow` | 1 | play the carousel; off = always your own preset (still synced to the grid) |
| `carouselBeats` | 8 | beats per carousel slot |

## Verified / unverified (2026-08-01)

Verified on one AtomS3: beacons TX at 100% send-callback success alongside BLE
advertising, host tests for ranking/slots/layouts. **Not yet verified** (needs the
Echo back from fork A, or any second device): live two-node grid convergence, carousel
lockstep, BLE app connect during mesh (coex under real BLE traffic), two confident
mics fighting (should resolve by MAC; watch for period thrash). Power draw of the
wifi radio is unmeasured — `mesh 0` is the fallback.

Debug serial: `mesh tx/rx` counters every 5s, `MESH err` per applied beacon,
following/leading transitions, `hello`/`lost` per neighbor.
