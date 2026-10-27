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
* **knob**: a potentiometer on an analog pin that writes one setting. Not "pot" in the
  app.
* **target**: the setting a knob writes, such as `brightness` or `color1.h`.
* **layer gate**: the runtime on/off state of a layer that switches and buttons
  control; fades the layer rather than cutting it.
* **preset**: one of the five stored sets of layer settings.
* **layer**: one of the ten composited animations in a preset.
* **mesh**: the ESP-NOW sync between nearby cores. See [mesh-sync.md](mesh-sync.md).
