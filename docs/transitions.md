# Transitions

Animations render from `renderedLayers`, not from the settings. Every frame,
`applyDerivedState` moves `renderedLayers` toward the target settings (the current
preset's layers, or the slot that the mesh carousel has on stage) in one of two ways.
The constants live in Transition.h; `test/transitiontest.cpp` checks them.

## Ease

A small change eases: each frame, the rendered value moves `1 - e^(-dt/0.1 s)` of the
way to its target, and a value close to its target lands on it. Easing follows a
slider drag smoothly, because a new target mid-ease only changes where the ease heads.

* Colors round away from the current value, so the last step lands.
* `speed` eases in log space. It is seconds (or beats) per cycle, but the eye sees its
  inverse. Easing it linearly made slow-to-fast take over a second and fast-to-slow
  look instant.
* Brightness and the layer gate ease the same way.

## Crossfade

A large change crossfades: `renderedLayers` snaps to the target, and for 0.5 s the
strip dissolves from a copy of the last displayed frame to the new scene. A change is
large when any layer's animation or blend mode differs, a color channel moves by more
than 48, or `speed`, `tau` or `phi` changes by more than a factor of 1.5 (or changes
sign, or moves to or from zero).

Easing large changes looks wrong. `tau` and `phi` divide the pixel index
(`curve(t - i/tau)`), so easing `tau` from 10 to 50 shifts pixel 400's phase by
40 - 8 = 32 cycles, and the pattern races through intermediate shapes.

The outgoing frame is frozen, not re-rendered. That costs one 2.4 KB buffer and no
second render pass, and a new crossfade mid-fade starts from the blended frame on the
strip, so a fade never pops. The cost: the outgoing scene stops moving while it fades.

## The 0.5 s bound

The crossfade thresholds bound how far an ease travels, and the time constant bounds
how long that takes: the largest change that still eases lands within 0.5 s at any
frame rate from 10 to 200 fps. Raising a threshold or the time constant breaks that
bound; transitiontest fails when it does.

`loop` caps a frame's `dt` at 0.1 s, so below 10 fps every transition runs in slow
motion.

## At boot

`renderedLayers` starts at the struct defaults and the first frame is black, so the
first frame crossfades from black to the loaded preset while the brightness eases up
from 0.
