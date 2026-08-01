# Beat detection

BeatDetector.h, three cooperating layers. Verified on an M5Atom Echo 2026-08-01/02;
every rule below came from a measured failure, not theory.

## Band layer

16ms chunks at nominal 16kHz through M5Unified Mic_Class (PDM mode is automatic when
pin_bck is unset). Two filter chains per chunk: a kick band (two cascaded one-poles,
~130Hz) and a hat/snare band (signal minus a ~1kHz one-pole). Each band's flux
(half-wave-rectified energy increase, normalized by that band's running level so
genres auto-balance) is summed into an onset-strength envelope.

The envelope is indexed by **wall-clock 16ms slots**, not chunk count: the mic queue
is only 2 chunks deep so slow frames silently drop audio, and the PDM clock's
dividers measured ~7% off nominal — chunk-counting both compresses the timeline and
skews lag->seconds. Wall slots make lost audio an honest zero and lag->seconds exact.

## Tempo layer: autocorrelation

Every ~0.5s, the envelope of the last ~4s (smoothed [0.25 0.5 0.25]) is
autocorrelated over 60-180bpm lags. Score = r(lag) + 0.5*r(2*lag) (comb: prefers the
beat over its 8ths), times a log-Gaussian prior centered on 120bpm (breaks octave
ties: r(P) equals r(2P) for a uniform beat train). Parabolic interpolation refines
below slot size. A candidate disagreeing >10% for 2 consecutive evaluations wins a
RETEMPO; a peak weaker than 1.5x the score mean is ignored as unrhythmic.

## Phase layer: onsets pull the grid

Discrete onsets are detected in the kick band only (energy over rolling mean+1.8σ of
~0.8s, floor rms 120, 256ms refractory) — hats sit on ambiguous half-beats and get no
phase authority. Onsets within 0.35 beats of a predicted gridline pull the phase;
within 0.15 they build confidence. Above confidence 0.4 the grid fires the envelope
instead of raw onsets and freewheels through breakdowns (decay ~20s).

Guard rails and the failure each fixes:

| rule | failure |
|---|---|
| flux smoothing before autocorrelation | beat periods are rarely whole slots; unsmoothed 1-slot spikes miss alignment at integer lags — 140bpm read as 70 |
| tempo prior | octave choice on uniform trains is a coin flip |
| confidence reward window 0.15 beats, pull window 0.35 | the pull window covers 70% of a beat, so random room noise landed "on grid" more often than not and kept a phantom grid pulsing; reward must sit below chance level |
| REPHASE after 4 onsets outside the pull window | a half-beat-offset lock is a stable fixpoint: onsets at phase 0.5 forever, unreachable by the pull window, confidence starved |
| 2nd-order period correction gated on confidence | phase-chasing can hold a ~10% wrong period while "confident"; ungated, noise steers the tempo |
| kOnsetLatency expected-phase offset | detection lags sound (attack ramp + chunk + queue), so the grid locked audibly late; tune by ear against a metronome |

## Standard test battery

Generate tracks (kick sweep + pad + bassline + offbeat hats, python stdlib) and afplay
them near the device; capture serial per docs/building-and-testing.md. Passing state
as of 2026-08-02:

| track | result |
|---|---|
| 24 beats @100 / @140 | 100.7 / 139.5 |
| 126bpm with offbeat open hats louder than the kicks | 125.3, conf 0.95 |
| 16 beats @124, 10 beats silence, 8 beats | ticks through the gap, re-locks 124.6 |
| 16 beats @120 then 20 @150 | RETEMPO, locks 149.0 |
| 20s ambient room | zero GRID fires |

## Known limits / next directions

* **Two-step breakbeats (DnB) lock the kick pattern, not the beat**: a 174 two-step's
  kick self-similarity peaks at the 1.5/2.5-beat kick intervals, which beat the
  correct 87 half-time in a single summed envelope. Next step: per-band
  autocorrelation so the snare stream (dead regular on 2 and 4) votes independently.
* Breakcore stays out of reach of periodicity assumptions; the onset-reactive
  fallback is the intended behavior there.
* If per-band correlation isn't enough: full spectral flux via FFT/FHT.
