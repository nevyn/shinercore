# Getting started: the story both apps tell

A newcomer meets ShinerCore through three places: the home page
(https://nevyn.dev/shinercore), ShinerCore Flasher (Mac) and ShinerCore Remote (iPhone,
iPad, Mac). They tell one story in the same four steps, with the same names. Each app
tells its own steps in full and points to the other for the rest, so nothing is
explained twice or contradicted.

| # | Step | Told in full by | The other app says |
|---|------|-----------------|--------------------|
| 1 | **Get a board**: one of the M5Stack boards the Flasher lists | Flasher (shop links) | Remote: "a small M5Stack board", link to the home page |
| 2 | **Put ShinerCore on it** over USB | Flasher | Remote: "with ShinerCore Flasher on a Mac", link to the home page |
| 3 | **Connect an LED strip** | Remote's build guide | Flasher: "ShinerCore Remote shows you how, step by step" |
| 4 | **Play**: find the core in the list and light it up | Remote | Flasher: "open ShinerCore Remote" |

## Step 3, the wiring both apps refer to

Any WS2812-compatible strip works (also sold as NeoPixel). The board's Grove socket
carries everything the strip needs, so the simplest cable is an M5Stack Grove cable
(https://docs.m5stack.com/en/accessory/cable/Grove_Cable) cut in half:

* **red** to the strip's **+** (5V),
* **black** to the strip's **−** (GND),
* **yellow** to the strip's **Data In** (DIN). The arrows printed on the strip point away
  from Data In, toward Data Out; solder at the end the arrows start from.

The yellow wire is the board's LED pin: G26 on the Atom Lite and Atom Echo, G2 on the
AtomS3 Lite (the Remote's board layouts, `WiringDiagram.swift`). The white wire is a
second signal pin, free for a button, switch or knob ([wiring.md](wiring.md)).

## Terms

"Build guide" is the Remote's step-by-step for step 3. "Core" is a board running
ShinerCore ([glossary.md](glossary.md)); before step 2 it is just "a board".
