# Glossary

One term per concept. Use these in code, docs, the app and commits.

* **wiring**: a core's map from pin to role, edited on the app's Wiring tab and stored
  as one setting. See [wiring.md](wiring.md).
* **pin**: a user-reachable connection, named by the label printed on the case
  ("G32"). Not "GPIO" or "port" in user-facing text.
* **role**: what a pin does: nothing, switch, button, knob, or LEDs. Not "kind",
  "mode" or "binding".
* **claim**: built-in hardware (speaker, microphone) using a pin, which makes the pin
  unavailable to wiring.
* **front button**: the button on the face of the case. A wiring entry whose only role
  is button; not "built-in button" or "BtnA".
* **knob**: a potentiometer on an analog pin that writes one setting. Not "pot" in the
  app.
* **target**: the setting a knob writes, such as `brightness` or `color1.h`.
* **layer gate**: the runtime on/off state of a layer that switches and buttons
  control; fades the layer rather than cutting it.
* **preset**: one of the five stored sets of layer settings.
* **power-on preset**: the `bootPreset` setting: the preset that a core switches to,
  with its lights on, every time it powers on. Off resumes whatever was showing.
* **ease**: how a small settings change reaches the pixels: the rendered value glides
  to the target. Not "slew" or "tween". See [transitions.md](transitions.md).
* **crossfade**: how a large settings change reaches the pixels: the strip dissolves
  from the last frame to the new scene. Not "fade" or "morph".
* **layer**: one of the ten composited animations in a preset.
* **power cap**: the `maxCurrent` setting: an estimated current budget that bright
  frames are dimmed to fit. An estimate from frame data, never a measurement.
* **mesh**: the ESP-NOW sync between nearby cores. See [mesh-sync.md](mesh-sync.md).
* **render library**: the animations and blend modes in `src/render/`, behind the C
  API in `ShinerRender.h`. The firmware draws frames with it, and the Remote app
  draws its strip preview with it (a SwiftPM package). Not "shared code" or "engine".
* **parameter names**: what an animation calls its tau and phi parameters ("Tail
  length", "Head width"). tau and phi stay the property names on the wire.
