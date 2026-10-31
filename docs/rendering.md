# Rendering

The render library is the animations, the blend modes and the per-layer composite
step, in `src/render/`. The firmware draws every frame with it, and the Remote app
draws its strip preview with it, through the C API in
`src/render/include/ShinerRender.h`. Sharing the code keeps the preview from
drifting from the strip; a hand-written Swift port drifted the day it was written.

## Layout

* `include/ShinerRender.h`: the C API, the app's only entry point. C rather than
  C++, so that Swift imports it without C++ interop.
* `Render.h`: the C++ side that the firmware uses. `kAnimations` is the animation
  table: each entry's name, function, color use and parameter names.
  `kBlendModeNames` and `LayerBlendMode` are the blend modes. `compositeLayer` is
  one layer's step of a frame: render the animation into a scratch buffer, then
  blend it into the frame at the layer's gain. Gain is how strongly the layer mixes
  in, 0-1: the mix between the frame without the layer and the frame with it fully
  blended in. The firmware passes the layer gate's fade times the layer's opacity.
* `RenderMath.h`: private helpers (`curve`, `hash`, gamma, color scaling).
* `Animations.cpp`, `Composite.cpp`, `ShinerRender.cpp`: the implementations.
* `host/FastLED.h`: the FastLED stand-in for builds that aren't the firmware.

`Package.swift` at the repository root exposes `src/render/` alone as the SwiftPM
library product `ShinerRender`, in C++11 like the ESP32 toolchain. The app pins the
package by revision.

The library has no Arduino, M5Unified, OverAnimate or BeatDetector dependency. The
firmware passes in what those provided: `LayerAnimation::animate` hands
`compositeLayer` the strip buffers, the layer's eased settings, its time in cycles,
the beat envelope and its gain.

## The C API is a contract

The Remote app ships separately from the firmware and pins one revision of this
package, so the C API changes additively only: add functions, but never change a
signature, a struct's layout or a documented meaning. The doc comments in
`ShinerRender.h` are the contract.

## Adding an animation

1. Write the function in `Animations.cpp`. It must be a pure function of its
   arguments (see AGENTS.md); use `hash()` for randomness.
2. Append one entry to `kAnimations`: name, function, how many colors it reads, and
   the names of tau and phi (`NULL` for one it ignores). Never reorder or insert:
   indices are persisted in NVS and sent over the mesh. Bump `AnimationCount`.
3. The names reach the app through the `documentation` and `animationDocs`
   characteristics ([ble-comms.md](ble-comms.md)). `animationDocs` must stay within
   512 bytes; `test/rendertest.cpp` checks that.
4. Update the expected table and `animationDocs` string in `test/rendertest.cpp`.

## Host fidelity

On the device, `#include "FastLED.h"` finds the real FastLED; everywhere else,
`host/FastLED.h` stands in. The stub provides only what the library uses, and its
arithmetic is FastLED 3.6's C paths copied verbatim (saturating `+`/`-`, `scale8`,
`blend8`, `lerp8by8`, `random8`, `hsv2rgb_rainbow`). Change it only by copying from
FastLED again, never by approximating. `test/stubs/FastLED.h` includes it, so the
host tests have one CRGB.

C++ leaves an out-of-range double-to-integer conversion undefined. Rainbow relies on
one: its hue wraps when the drift is negative. On the device, libgcc's `__fixdfsi`
and `__fixunsdfsi` decide the result, so `toInt` and `toByte` in `RenderMath.h`
compile to a plain cast for Xtensa and reproduce libgcc's result everywhere else.
Without them, an arm64 host drew a backwards rainbow as solid red.

## Tests

`test/rendertest.cpp` checks the table, `animationDocs`, the C API's edge cases, and
274 golden frames in `test/rendergoldens.h`: every animation with three parameter
sets at five times, and every blend mode at four gains. The goldens are what the
firmware drew before the library existed, captured on the host from that code with
the conversions above pinned to libgcc's. When a change to an animation's output is
intended, regenerate them with `test/build/rendertest --print-goldens`.

The goldens run on the host. To check that a refactor leaves the device's frames
unchanged, compare each animation's floating-point instructions and soft-float calls
(`xtensa-esp32-elf-objdump -dr` of its object file) before and after: the same
operations on the same values give the same pixels, even when reordered.
