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
2. **ArduinoBLE local patch**: `_pendingPkt = 0` on disconnect, in
   `~/Dev/Arduino/libraries/ArduinoBLE/src/utility/HCI.cpp` (`EVT_DISCONN_COMPLETE`
   handler, marked "ShinerCore local patch"). That library is **not** a git checkout
   and not pinned — a library update silently reverts the fix. If sliders ever wedge
   a core again, check this patch is still present. Correct upstream too: after a
   disconnect the controller has flushed that link's packets and reports nothing.
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
harness lives and dies with its session scratchpad; it's ~100 lines of
CBCentralManager worth rewriting if needed (subscribe, write storm, cancel at
0.8s, reconnect pinned to the same peripheral identifier, reread).

## Timing contracts the app can rely on

* A write is *applied* (visible in the LEDs) the same frame it's polled; only the
  canonical echo is delayed (≤ ~50ms + queue position).
* Only-latest: intermediate drag values may never be notified; the settled value
  always is.
* A cursor change (layer/preset) republishes all 8 per-layer characteristics, spread
  over ~½s by the flush limiter.
