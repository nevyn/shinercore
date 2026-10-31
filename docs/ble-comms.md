# BLE comms: the property protocol and its failure modes

Properties (StoredProperty.h, wired up in Comms.h) expose every setting as a string
characteristic. The typed field is the single source of truth; a write decodes into
RAM immediately, and the two projections — the NVS string and the BLE characteristic
value/notification — trail behind it. This decoupling is a survival requirement, not
an optimization; the coupled version hung real hardware. Symptoms and mechanism below.

## The wedge (2026-08: core hangs while dragging sliders in the app)

Every app-side slider tick is a BLE write. The write path used to synchronously do
both projections: an NVS commit (a flash write per tick) and a notify echo through
`BLECharacteristic::writeValue`. ArduinoBLE's `HCIClass::sendAclPkt` busy-waits for
the controller's ACL credits with no timeout:

```cpp
while (_pendingPkt >= _maxPkt) { poll(); }
```

Notifies, and the ATT write *responses* the incoming stream itself requires, all draw
from that same small credit pool. A drag saturates it, so the loop spends its time in
that wait — frames come out hundreds of ms apart, and each renders with a giant time
delta (perceived as flicker/strobing). If the link then drops mid-drag (phone lock,
range, 2.4GHz contention with the ESP-NOW mesh), stock ArduinoBLE never resets
`_pendingPkt` on `EVT_DISCONN_COMPLETE`. The credits are leaked; the *next*
`sendAclPkt` — one more notify — spins forever. Whole-core hang, LEDs frozen.

## The three defenses

1. **Decoupled projections** (StoredProperty.h): `publish()` stages only-latest;
   commsUpdate flushes at most one characteristic per 50ms, round-robin. `persist()`
   debounces 0.5s of settle, so a drag is one flash write, not hundreds. The pending
   NVS key is captured at set time, so a cursor move mid-debounce can't misfile it;
   `loadLayers` flushes pending persists before reading the store back.
2. **ArduinoBLE fork**: `_pendingPkt = 0` on disconnect (`EVT_DISCONN_COMPLETE`
   handler in `src/utility/HCI.cpp`). `~/Dev/Arduino/libraries/ArduinoBLE` is a git
   checkout of github.com/nevyn/ArduinoBLE, branch `shinercore` = upstream tag 1.3.6
   plus the fork commits below. On any other build machine, replace the
   library-manager copy with the same checkout:
   `git clone -b shinercore git@github.com:nevyn/ArduinoBLE.git ~/Dev/Arduino/libraries/ArduinoBLE`.
   The fix is correct for upstream too (after a disconnect the controller has flushed
   that link's packets and reports nothing), modulo multi-connection credit
   accounting — worth a PR someday.
3. **Task watchdog** (shinercore.ino setup): 8s, panic=reboot. Any residual hang —
   this one, or the next one — becomes a short outage that reboots into
   NVS-restored settings instead of a dead jacket.

The loop also clamps its frame delta to 100ms, so any future stall plays out as
slow motion rather than a phase teleport ("time never jumps").

Verified 2026-08-02 with an accidental A/B: a CoreBluetooth script stormed 200
tau writes at a patched core and yanked the connection mid-flight — it kept
rendering and meshing, echoed notifies at exactly the 50ms limit, and made one
debounced NVS write. An unpatched core in the same room hung within a minute of
a mere connect+subscribe+read+abrupt-disconnect from the same script. The storm
harness is test/blestorm.swift (macOS, CoreBluetooth; usage in its header —
run it from a real terminal, TCC kills agent shells).

## Empty values are readable (second fork commit)

Stock ArduinoBLE's ATT read handler treats `offset >= valueLength` as Invalid
Offset (0x07), so a characteristic holding a zero-length value fails every read
(a plain read is offset 0, and `0 >= 0`). The spec reserves that error for
offset *greater than* the length. The only property whose encoding can be empty
is meshGroup (default "" = the everyone-group): on an un-camped core the app's
read got 0x07, surfaced by CoreBluetooth as "The offset is invalid". Notifies
were unaffected — zero-length notifies are legal — so the value still arrived,
just behind an error banner. Fixed in the fork (`>` instead of `>=`, ATT.cpp);
the app also treats an invalid-offset read as "" for cores flashed before this.

## Notifications carry at most 20 bytes

ArduinoBLE never raises the ATT MTU above the default 23, so a notification holds at
most 20 bytes of value; longer values are truncated. Reads and writes can be longer
(Read Blob and Prepared Write both work in ArduinoBLE). A value that may exceed 20
bytes therefore gets no notify, and a short notifying characteristic tells the app when
to re-read it: `wiring` and `pinStatus` in [wiring.md](wiring.md).

## Timing contracts the app can rely on

* A write is *applied* (visible in the LEDs) the same frame it's polled; only the
  canonical echo is delayed (≤ ~50ms + queue position).
* Only-latest: intermediate drag values may never be notified; the settled value
  always is.
* A cursor change (layer/preset) republishes all 8 per-layer characteristics, spread
  over ~½s by the flush limiter.

## Read-only vocabularies

A characteristic value holds at most 512 bytes: that is the ATT maximum attribute
length in the Bluetooth core spec, not an ArduinoBLE setting, and Read Blob cannot
read past it. A vocabulary that would outgrow 512 bytes therefore gets its own
characteristic instead of a new key in an existing one. Changing the type of an
existing key breaks older apps, which decode the whole value or nothing.

* `documentation` (`76db9199-21af-4207-a23c-dc138a6cd42d`): the names that the
  index-valued properties count into, as
  `{"blendModes":[...],"animations":[...],"ledColorOrders":[...]}`.
* `animationDocs` (`5a9ce2c0-4837-4bd4-825f-ae639fdccc31`): one entry per animation,
  in the order of `documentation.animations`. An entry is `[colors]`,
  `[colors,"tau name"]` or `[colors,"tau name","phi name"]`; `colors` is 0, 1 or 2,
  how many of `color` and `color2` the animation reads, and `""` names a parameter
  that the animation ignores. Example: `[[0],[1],...,[2,"Tail length","Head width"],...]`.
  Built from the render library's animation table ([rendering.md](rendering.md)).
  Cores without it predate it; the app then falls back to its own copy of the table.
* `wiringDocs`: see [wiring.md](wiring.md).

## The layer summary

The per-layer characteristics show one layer at a time (the cursor). These show the
whole stack, so the app can list every layer and preview the composite.

* `layers` (`a988b500-1b73-48e7-b353-9612e873af48`, read): what the strip is playing
  now, as text lines separated by `\n`. The first line says whose preset it is: `P n`
  for the core's own preset `n`, or `M` while the mesh carousel plays a neighbor's
  preset (during a slot change, layers cross over one at a time, and the header says
  `M` until all of them play the core's own preset again). Then one line per layer
  whose animation is not Nothing, bottom layer first:

  ```
  index animation blend color color2 speed tau phi beatSync gate
  ```

  `animation` and `blend` index `documentation.animations` and `documentation.blendModes`;
  colors are six lowercase hex digits `rrggbb`; floats have at most 3 decimals with
  trailing zeros and a trailing point dropped (`2.5`, `10`, `-0.125`); `beatSync` is
  0 or 1; `gate` is 1 while the layer gate is open (the layer shows) and 0 while a
  switch or button has it closed. Example: `P 1\n0 11 0 ff6400 f0ff00 6 6 4 0 1`.
  The compact formats keep ten worst-case layers under 512 bytes (493). The values are
  the settled targets, not the eased in-between values on the strip. The firmware
  stores the text synchronously, as `wiring` does: a characteristic without notify
  does no radio I/O on update. It reconciles rather than tracking triggers: every
  100 ms it formats what the strip plays (`applyDerivedState`, from the carousel's
  targets and the layer gates) and publishes only a changed text.
* `layersRevision` (`9231bed4-aec8-4fa0-88be-3242e937fc0a`, read, notify): a decimal
  counter (0-65535, wrapping). It changes whenever `layers` would read differently:
  a layer edit from BLE, a knob or the mesh, a preset change, a layer move, a layer
  gate opening or closing, and a carousel slot change. Changes coalesce: at most one
  notification per 100 ms, and the settled state is always notified. The app re-reads
  `layers` when the counter changes.
* `layerMove` (`df64df61-5728-4e42-809f-bb7735fb30a0`, write): `FROM TO`, two layer
  indices 0-9. Moves layer FROM of the current preset to index TO, shifting the
  layers in between (remove, then insert). The move is one edit: it persists, reaches
  the mesh like any other edit, and bumps `layersRevision`. A layer cursor on FROM
  follows the layer to TO. Wiring and layer gates stay with their index, because
  wiring names layer slots across all presets ([wiring.md](wiring.md)). Malformed or
  out-of-range values are ignored and logged to serial.
  The move persists only the fields whose value changed. Each property holds one
  pending NVS write, so all but the last changed layer of each property are written
  synchronously in the frame that applies the move: a few writes for a typical stack,
  at most 72 for ten full layers. A move is a single write per drag, not a stream.

Cores without these characteristics predate them; the app then shows the cursor's
layer alone.
