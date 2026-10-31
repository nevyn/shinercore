# Documentation index

* [building-and-testing.md](building-and-testing.md) — build/flash commands for all three
  board variants, host tests, serial monitoring, hands-free audio testing.
* [beat-detection.md](beat-detection.md) — the onset detector and beat-tracking PLL:
  design, tunings that came from measurement, known failure modes, planned directions.
* [transitions.md](transitions.md) — how settings changes reach the pixels: small ones
  ease, large ones crossfade, and both finish within 0.5 s.
* [mesh-sync.md](mesh-sync.md) — ESP-NOW sync: shared beat grid with implicit leader
  election, the preset carousel, wire formats, what's verified vs pending hardware.
* [ble-comms.md](ble-comms.md) — the BLE property protocol: why its side effects are
  decoupled from writes, the ArduinoBLE credit-leak wedge and local patch, watchdog,
  the layer summary and layer move.
* [wiring.md](wiring.md) — pin roles (switch, button, knob, LEDs): boards and pins,
  the `wiring` JSON, the BLE characteristics and their MTU limits, runtime semantics.
* [rendering.md](rendering.md) — the render library in `src/render/`: the animation
  table, the C API and SwiftPM package that the Remote app draws its strip preview
  with, host fidelity, golden frames, adding an animation.
* [firmware-builds.md](firmware-builds.md) — CI, the firmware build layout and manifest,
  the fingerprint, and why the flasher never writes a merged image.
* [glossary.md](glossary.md) — the canonical term for each concept.

The architecture brief and its invariants are in [../AGENTS.md](../AGENTS.md).
The user-facing project readme is [../README.md](../README.md).
