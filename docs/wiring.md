# Wiring: pin roles, knobs, and their BLE protocol

A core's **wiring** maps each user-reachable pin to a **role**: nothing, switch, button,
knob, or LEDs. The app edits it on the Wiring tab; the firmware validates, stores and
runs it. Terms are defined in [glossary.md](glossary.md).

## Boards and pins

Pins are named by the label printed on the case. Only ADC1 pins can be knobs: ADC2
(GPIO 0, 2, 4, 12-15, 25-27) stops working while wifi is up, and the ESP-NOW mesh keeps
it up. FastLED binds an output pin at compile time, so only the two Grove signal pins
can be LEDs.

| Board id | Pins (status bit order) | Analog | LED-capable | Claims |
|---|---|---|---|---|
| `atom-lite` | G21 G25 G22 G19 G23 G33 G26 G32 | G33 G32 | G26 G32 | G23, G33: `microphone` while `mic` is on |
| `atom-echo` | G21 G25 G22 G19 G23 G33 G26 G32 | G32 | G26 G32 | G19, G22: `speaker`; G23, G33: `microphone` (always) |
| `atoms3-lite` | G39 G38 G5 G6 G7 G8 G2 G1 | G5 G6 G7 G8 G2 G1 | G2 G1 | none |

Grove cable colors: black G, red 5V, yellow G26 (S3: G2), white G32 (S3: G1). The
built-in button is not a wiring pin; it keeps its fixed behavior.

### Defaults

Absence of the stored `wiring` key means the board default, which reproduces the
pre-wiring firmware exactly:

* `atom-lite`: G26, G32 LEDs; G21 button `nextPreset`; G25 switch `lightsOff`;
  G22, G19, G23, G33 switch `preset` 0, 1, 2, 3.
* `atom-echo`: G26, G32 LEDs; G21 button `nextPreset`; G25 switch `lightsOff`.
* `atoms3-lite`: G2, G1 LEDs; G39 button `nextPreset`; G38 switch `lightsOff`;
  G5, G6, G7, G8 switch `preset` 0, 1, 2, 3.

## Wire format: the `wiring` value

JSON object keyed by pin label. A pin that is absent has role nothing. Each value is an
object whose first key names the role:

```json
{"G26":{"leds":true},
 "G21":{"button":"nextPreset"},
 "G25":{"switch":"lightsOff"},
 "G22":{"switch":"preset","preset":0},
 "G23":{"switch":"layerOn","layer":3},
 "G32":{"knob":"brightness","min":0,"max":255},
 "G33":{"knob":"color1.h","layer":2,"min":0,"max":255}}
```

| Role | Value | Extra keys |
|---|---|---|
| LEDs | `{"leds":true}` | none |
| switch | `lightsOff`, `lightsOn`, `preset`, `layerOn` | `preset` (0-4) or `layer` (0-9) where the action needs one |
| button | `nextPreset`, `toggleLights`, `preset`, `toggleLayer` | as for switch |
| knob | a target id (below) | `layer` (0-9) for layer targets; `min`, `max` in target units |

Knob targets, their units and valid range:

| Target | Setting | Range | Curve |
|---|---|---|---|
| `brightness` | brightness | 0-255 | square law |
| `speed` | layer speed | 0.001-100 | logarithmic when min, max > 0 |
| `tau`, `phi` | layer tau, phi | -100-100 | logarithmic when min, max > 0 |
| `color1.r` `.g` `.b` `.h` `.s` `.v` | primary color channel | 0-255 | linear |
| `color2.r` ... `color2.v` | secondary color channel | 0-255 | linear |

`min > max` reverses the knob. Hue, saturation and value use FastLED's 0-255 HSV scale.

### Validation

A write is rejected whole, and the stored wiring left unchanged, if any of these hold:
it doesn't parse; a key isn't a pin of this board; a pin has a permanent claim; a role
isn't allowed on that pin (knob needs analog, LEDs needs LED-capable); an action or
target is unknown; a required extra key is missing or out of range; an unexpected key
is present; `min` or `max` is outside the target's range. A pin with a conditional claim
(the Atom Lite's mic pins) may hold a role; the role is inactive while claimed.

The firmware re-encodes accepted wiring canonically: pins in status bit order, keys in
the order shown above, numbers in their shortest form. Clients compare wiring by
meaning, not bytes. An empty write resets to the board default.

## BLE characteristics

ArduinoBLE caps the ATT MTU at 23, so a notification carries at most 20 bytes. Reads
and writes may be longer (Read Blob, Prepared Write). Hence:

| Name | UUID | Properties | Size |
|---|---|---|---|
| `wiring` | `2be57867-0fd5-45b7-93af-c9edff69fe60` | read, write | <= 512 |
| `pinStatus` | `53775889-c119-4673-89f2-359a80fabcca` | read, notify | <= 20 |
| `wiringDocs` | `682bf183-44f6-4dc9-9eb0-640c05ae45db` | read | <= 512 |

**`wiring`** holds the canonical wiring. It has no notify. The firmware stores the
canonical value into the characteristic synchronously while handling the write (a
characteristic without notify does no radio I/O on update), then increments the
`pinStatus` revision. NVS persistence debounces like every other property.

**`pinStatus`** is lowercase hex, fixed width, no separators:

```
rr cc mm pp pp ...
```

* `rr`: wiring revision, mod 256. Increments on every applied, reset or rejected
  `wiring` write. A client re-reads `wiring` when it changes.
* `cc`: closed bitmask; bit *i* is pin *i* in the board's status bit order. Set when the
  pin reads LOW (connected to G). Zero for knob and LED pins.
* `mm`: claimed bitmask, same bit order.
* `pp`: one byte per analog-capable pin, in status bit order: the filtered knob position
  0-255. Present for every analog-capable pin whatever its role. A pin without a knob
  reports its digital state: ff open (the pull-up), 00 closed; LED and claimed pins
  report 00. The firmware never samples those pins with the ADC: on the ESP32 core,
  `analogRead` switches the pad to analog mode, which disables the pull-up
  (`__adcAttachPin` in esp32-hal-adc.c, core 2.1.1).

Example, Atom Echo, G21 pressed, knob on G32 at 62%: `07` `01` `3c` `9e` =
`07013c9e`. The firmware publishes on change only, at most every 100 ms, through the
shared notify flush limiter.

**`wiringDocs`** is static JSON, read once per connection:

```json
{"board":"atom-echo",
 "pins":["G21","G25","G22","G19","G23","G33","G26","G32"],
 "analog":["G32"],
 "led":["G26","G32"],
 "claims":{"G19":"speaker","G22":"speaker","G23":"microphone","G33":"microphone"},
 "targets":["brightness","speed","tau","phi","color1.r","color1.g","color1.b",
            "color1.h","color1.s","color1.v","color2.r","color2.g","color2.b",
            "color2.h","color2.s","color2.v"]}
```

`claims` names every pin that can be claimed and by what; `pinStatus` says whether the
claim is active right now.

## Runtime semantics

Inputs read with `INPUT_PULLUP`: closed means connected to G. Every digital input is
debounced (30 ms stable).

**Switches hold their state while closed** and are reconciled every frame, not fired
once:

* `lightsOff`: closed holds mode Off; opening sets On.
* `lightsOn`: closed holds mode On; opening sets Off. A closed `lightsOff` beats it.
* `preset`: closed holds that preset. The first closed preset switch in bit order wins.
* `layerOn`: the layer's gate follows the switch, open meaning off.

**Buttons act on press:** `nextPreset` cycles on release and toggles lights after a
600 ms hold, as the built-in button does; `toggleLights`, `preset` and `toggleLayer` act
on press.

**Layer gates** are runtime-only, start open at boot, and are not stored or sent over
BLE. A gate fades its layer in and out with the settings slew (`renderedLayers`); the
layer composites as `blend(under, blendMode(under, layer), gain)`. A switch on a layer
overrides any button toggle of the same layer.

**Knobs write the target setting** through the same path as an app write, so the value
persists, notifies, and reaches the mesh like any edit. Layer targets address layer
*N* of the current preset, independent of the BLE layer cursor.

* The reading is low-pass filtered (50 ms), with the outer 1% of travel at each end
  snapped to that end so 0 and 255 are reachable; the travel between is stretched to
  fit, so the snap doesn't jump.
* A knob writes only when its filtered position moves past a deadband (0.6% of travel)
  from the last position it wrote, or reaches an end, and never during the first 0.5 s
  after its role starts (boot, a wiring change, a claim ending): a knob only edits
  when someone turns it.
* Two knobs turned at once that address the same setting on different layers each
  flush the other's pending NVS write, so they persist every frame instead of once.
* HSV channels hold the HSV value they last wrote and only re-derive it from the stored
  color if something else changed that color, so the other two channels don't drift.

**LED pins** are registered with FastLED at boot. A write that changes which pins have
the LED role restarts the core 0.3 s later, after flushing every pending persist. Until
then, the pins registered at boot stay FastLED's and a new LED pin stays untouched. The
power cap (`maxCurrent`) splits evenly across the registered outputs.

**Claimed pins** are never configured or read by the wiring runtime. When a claim ends
(mic turned off), the pin's role resumes from rest, without firing edges.

## Wiring hardware

* Switches and buttons go between the pin and G. Knobs: outer legs to 3V3 and G,
  wiper to the pin. Never 5V: the Grove cable's red wire is 5V, and more than 3.3 V
  damages the pin.
* Use a 10 kΩ linear (B10K) pot. A 1 kΩ resistor in series with the wiper protects the
  pin if the pin is ever driven as an output, such as by older firmware that mirrors
  LED data on both Grove pins. It doesn't affect the reading.
