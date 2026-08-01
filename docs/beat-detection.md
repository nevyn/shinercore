# Beat detection

BeatDetector.h, two cooperating layers. Verified on an M5Atom Echo 2026-08-01; every
tuning below came from a measured failure, not theory.

## Onset layer

16ms chunks at 16kHz through M5Unified Mic_Class (PDM mode is automatic when pin_bck
is unset). One-pole LPF ~160Hz isolates the kick band; chunk energy over the rolling
mean+1.8σ of ~0.8s is an onset. Floor rms 120 gates silence (quiet-room ambient
measured ~40; floor 400 ate quiet music whose kicks peaked ~450).

## Tempo layer: a PLL over onsets

Inter-onset intervals, interpreted as whole/half beat multiples of the current period
when close (else folded into the 60-180bpm octave as retempo evidence), drive the
period via a ring **mean**. Onsets near a predicted gridline pull phase; once
confidence passes 0.4 the grid fires the envelope instead of raw onsets and
freewheels through breakdowns (confidence decays over ~20s).

Guard rails and why they exist:

| rule | failure it fixes |
|---|---|
| ring mean, not median | detection quantizes to frame boundaries and dwells a few ms off before snapping back; the lopsided distribution biased a median ~2% low |
| half-beat interval interpretation | offbeat hats and missed kicks produced 1.5/2.5-beat intervals that folded into wrong-tempo evidence (124 -> 113bpm after breakdowns) |
| a run of >=4 equal raw intervals overrides interpretation | beat-multiple interpretation holds a 3:2 lock forever (0.6s onsets read as 1.5-beat gaps at 150bpm); tolerance 10% because quantization makes even a metronome alternate ~6% |
| confidence reward window 0.15 beats, pull window 0.35 | the pull window covers 70% of a beat, so random room noise landed "on grid" more often than not and kept a phantom grid pulsing; reward must be below chance level |
| 2nd-order period correction gated on confidence | phase-chasing can hold a ~10% wrong period at conf 1.0; but ungated, noise steers the tempo |
| kOnsetLatency expected-phase offset | detection lags sound ~50-90ms (attack ramp + chunk + queue + render), so the grid locked audibly late; tuned by ear, only helps in grid mode |

## Standard test battery

Generate tracks (kick sweep + pad + bassline + offbeat hats, python stdlib) and afplay
them near the device; capture serial per docs/building-and-testing.md. Passing state
as of 2026-08-01:

| track | expectation |
|---|---|
| 24 beats @100 / @140 | locks within 0.5% of truth, conf > 0.9 |
| 16 beats @124, 10 beats silence, 8 beats | grid ticks through the gap, re-locks ~124 |
| 16 beats @120 then 20 @150 | RETEMPO within ~8 beats, locks ~150 |
| play 150 track then 100 track | escapes the 3:2 trap via steady-interval override |
| 15s ambient room | confidence stays ~0, no phantom GRID fires |

## Known limits / next directions

* **Hats capture the lock on real music**: broadband hat energy leaks through the
  one-pole LPF, and hat onsets can outnumber kicks. Reported drifty/hat-locked on
  minimal techno 2026-08-01.
* **Breakbeats (DnB, breakcore) defeat interval-pair logic** by design: syncopated
  kicks never produce steady intervals. Falls back to reactive onset flashing (which
  reads as intentional chaos, but isn't tracking).
* Planned, in order: (1) autocorrelation/comb over an onset-strength envelope instead
  of pairwise intervals — finds the pulse under syncopation; (2) a second detection
  band (snare/hat flux) as an independent onset stream voting on the grid; (3) full
  spectral flux via FFT/FHT if still needed.
