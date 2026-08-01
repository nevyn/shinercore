#ifndef __BEAT_DETECTOR__H
#define __BEAT_DETECTOR__H
#include "M5Unified.h"
#include <OverAnimate.h>
#include "Util.h"

// Beat detection from an M5Atom Echo's PDM microphone (SPM1423, CLK on G33,
// DATA on G23), captured through M5Unified's Mic_Class. Mic_Class selects PDM
// mode automatically when pin_bck is unset.
//
// Two cooperating layers:
//
// Onsets, energy-based, no FFT: each 16ms chunk of audio is low-pass filtered
// to keep the kick-drum band, and its energy is compared against the mean and
// deviation of the last ~0.8s. An energy spike is an onset.
//
// Tempo, a phase-locked loop over the onsets: intervals between onsets are
// folded into one 60-180bpm octave and their median drives the period; onsets
// landing near a predicted beat pull the phase into lock and raise confidence.
// Once confident, the grid fires the envelope instead of raw onsets - steady
// through syncopation - and keeps ticking through breakdowns and quiet parts,
// with confidence slowly decaying until onsets return.
//
// envelope() decays 1 -> 0 from each beat for animations to render;
// phase()/bpm()/confidence() expose the clock itself.
//
// The Echo can't be detected at runtime (same chip as the Atom Lite, and a PDM
// mic can't be probed), so this only runs when the "mic" setting is on.
class BeatDetector
{
public:
    void begin()
    {
        if(_running) return;
        auto cfg = M5.Mic.config();
        cfg.pin_data_in = 23;
        cfg.pin_ws = 33;
        cfg.sample_rate = kSampleRate;
        M5.Mic.config(cfg);
        _running = M5.Mic.begin();
        if(_running)
        {
            M5.Mic.record(_chunks[0], kChunkSamples);
            M5.Mic.record(_chunks[1], kChunkSamples);
            _nextChunk = 0;
        }
        logger.print("mic: "); logger.println(_running ? "recording" : "failed to start");
    }
    void end()
    {
        if(!_running) return;
        M5.Mic.end();
        _running = false;
    }
    bool running() const { return _running; }

    void update(TimeInterval delta)
    {
        _now += delta;
        _envelope *= expf(-delta / kEnvelopeDecay);
        _confidence *= expf(-delta / kConfidenceDecay);

        // the beat grid ticks even when the room is quiet
        _beatPhase += delta / _period;
        _totalBeats += delta / _period;
        if(_beatPhase >= 1.0f)
        {
            _beatPhase -= (int)_beatPhase;
            if(_confidence > kConfidentThreshold)
            {
                _envelope = 1.0f;
                if(kDebugAudio) Serial.printf("GRID  %5.1fbpm conf %.2f\n", bpm(), _confidence);
            }
        }

        if(!_running) return;

        // Consume finished chunks and hand their buffers back; two are always
        // queued, so the mic task never starves and we never block.
        int guard = 4;
        while(M5.Mic.isRecording() < 2 && guard--)
        {
            analyze(_chunks[_nextChunk]);
            M5.Mic.record(_chunks[_nextChunk], kChunkSamples);
            _nextChunk ^= 1;
        }

        if(kDebugAudio)
        {
            _sinceDebugPrint += delta;
            if(_sinceDebugPrint > 0.25)
            {
                _sinceDebugPrint = 0;
                Serial.printf("mic rms %6.0f  avg %6.0f  env %.2f | %5.1fbpm conf %.2f\n",
                              sqrtf(_lastEnergy), sqrtf(_meanEnergy), _envelope, bpm(), _confidence);
            }
        }
    }

    /// 1.0 at the moment of a beat, exponentially decaying to 0. Render this.
    float envelope() const { return _envelope; }
    /// Position within the current beat, 0..1, phase-locked to the music.
    float phase() const { return _beatPhase; }
    /// Beats elapsed since boot, fractional, phase-locked. Can retreat by a
    /// fraction of a beat on a phase correction; consumers chase, not jump.
    TimeInterval beatTime() const { return _totalBeats; }
    float bpm() const { return 60.0f / _period; }
    float period() const { return _period; }
    /// 0..1: how well recent onsets have matched the predicted grid.
    float confidence() const { return _confidence; }

    /// Evidence from a more confident neighbor's grid (Mesh.h decides whose).
    /// Pulls period, phase and the whole-beat count toward the leader's;
    /// corrections mirror into beatTime so beat-synced layers glide along, and
    /// agreeing on the beat number aligns their cycles across cores.
    void applyNetworkBeat(float period, float phase, double beatTime, float confidence)
    {
        _period += (period - _period) * kPeriodGain;

        float err = (float)remainder((double)_beatPhase - (double)phase, 1.0); // signed beats ahead of the leader
        _beatPhase -= err * kPhaseGain;
        _totalBeats -= err * kPhaseGain;
        if(_beatPhase < 0) _beatPhase += 1.0f;
        if(_beatPhase >= 1.0f) _beatPhase -= 1.0f;

        double wholeBeats = round(beatTime - _totalBeats);
        _totalBeats += wholeBeats;

        float target = confidence * 0.9f;
        if(target > _confidence) _confidence = target;

        if(kDebugAudio) Serial.printf("MESH  err %+.3f%s %5.1fbpm conf %.2f\n",
                                      err, wholeBeats ? " (beat # adopted)" : "", bpm(), _confidence);
    }

private:
    static const int kSampleRate = 16000;
    static const int kChunkSamples = 256;            // 16ms per decision
    static const int kRingSize = 48;                 // ~0.8s of energy history
    static const int kRefractoryChunks = 16;         // >=256ms between onsets
    static constexpr float kBassAlpha = 0.061f;      // one-pole LPF at ~160Hz
    static constexpr float kSensitivity = 1.8f;      // onset = mean + this many std devs
    static constexpr float kEnergyFloor = 120.0f * 120.0f; // ignore near-silence; quiet room ambient is ~40 rms
    static constexpr float kEnvelopeDecay = 0.15f;   // seconds

    static const int kIOIRingSize = 8;               // onset intervals kept for the median
    static constexpr float kMinPeriod = 60.0f / 180.0f; // fold tempo into 60-180bpm
    static constexpr float kMaxPeriod = 60.0f / 60.0f;
    static constexpr float kPhaseWindow = 0.35f;     // beats: onsets this close to the grid pull phase
    static constexpr float kConfidenceWindow = 0.15f; // beats: only this close rewards confidence
    static constexpr float kPhaseGain = 0.5f;        // fraction of phase error corrected per onset
    static constexpr float kPeriodGain = 0.25f;      // fraction of period error corrected per onset
    static constexpr float kFreqGain = 0.05f;        // period correction from phase error (2nd PLL order)
    static const int kRetempoVotes = 3;              // consecutive disagreeing onsets before snapping tempo
    static constexpr float kConfidentThreshold = 0.4f; // above this, the grid drives the envelope
    static constexpr float kConfidenceDecay = 20.0f; // seconds; the grid outlives a breakdown
    static constexpr float kOnsetLatency = 0.06f;    // seconds detection lags the sound; tune by eye against a metronome

    static const bool kDebugAudio = true;            // serial meter for bring-up

    void analyze(const int16_t *samples)
    {
        float energy = 0;
        for(int i = 0; i < kChunkSamples; i++)
        {
            _bass += kBassAlpha * (samples[i] - _bass);
            energy += _bass * _bass;
        }
        energy /= kChunkSamples;
        _lastEnergy = energy;

        // rolling mean/variance over the ring
        _ring[_ringIndex] = energy;
        _ringIndex = (_ringIndex + 1) % kRingSize;
        float mean = 0;
        for(int i = 0; i < kRingSize; i++) mean += _ring[i];
        mean /= kRingSize;
        float var = 0;
        for(int i = 0; i < kRingSize; i++) var += (_ring[i] - mean) * (_ring[i] - mean);
        var /= kRingSize;
        _meanEnergy = mean;

        _chunksSinceOnset++;
        if(energy > kEnergyFloor
            && energy > mean + kSensitivity * sqrtf(var)
            && _chunksSinceOnset >= kRefractoryChunks)
        {
            _chunksSinceOnset = 0;
            if(kDebugAudio) Serial.printf("ONSET rms %6.0f over avg %6.0f phase %.2f\n", sqrtf(energy), sqrtf(mean), _beatPhase);
            onOnset();
        }
    }

    void onOnset()
    {
        // Until the grid is trustworthy, raw onsets drive the visuals
        if(_confidence <= kConfidentThreshold) _envelope = 1.0f;

        float ioi = _now - _lastOnsetAt;
        _lastOnsetAt = _now;
        if(kDebugAudio) Serial.printf("  ioi %.3f (period %.3f)\n", ioi, _period);
        if(ioi > 0.24f && ioi < 2.5f)
        {
            // A run of equal intervals IS the beat, whatever the current
            // hypothesis says. Without this override, beat-multiple
            // interpretation below can hold a 3:2 lock forever (0.6s onsets
            // read as 1.5-beat gaps at 150bpm).
            // 10% tolerance: detection quantizes to frames, so even a
            // metronomic pulse measures with a few percent of alternation
            if(fabsf(ioi - _lastRawIoi) < 0.10f * _lastRawIoi) _steadyCount++;
            else _steadyCount = 0;
            _lastRawIoi = ioi;
            if(_steadyCount >= 3)
            {
                float p = ioi;
                while(p > kMaxPeriod) p *= 0.5f;
                while(p < kMinPeriod) p *= 2.0f;
                if(fabsf(p - _period) / _period > 0.1f)
                {
                    if(kDebugAudio) Serial.printf("RETEMPO(steady) %5.1f -> %5.1fbpm\n", bpm(), 60.0f / p);
                    _period = p;
                    _confidence = 0.3f;
                    _retempoVotes = 0;
                    _ioiCount = 0; _ioiIndex = 0; // old tempo's evidence is void
                }
            }

            // An interval near a whole or half number of beats supports the
            // current tempo (missed kicks make 2-3 beat gaps, offbeat hats
            // make half-beat ones); anything else is folded into one octave
            // as evidence of a different tempo.
            float halfBeats = ioi / _period * 2.0f;
            int n = (int)roundf(halfBeats);
            if(n >= 1 && n <= 8 && fabsf(halfBeats - n) < 0.3f)
            {
                ioi = ioi * 2.0f / n;
            }
            else
            {
                while(ioi > kMaxPeriod) ioi *= 0.5f;
                while(ioi < kMinPeriod) ioi *= 2.0f;
            }
            _iois[_ioiIndex] = ioi;
            _ioiIndex = (_ioiIndex + 1) % kIOIRingSize;
            if(_ioiCount < kIOIRingSize) _ioiCount++;

            float med = ioiEstimate();
            if(fabsf(med - _period) / _period < 0.1f)
            {
                _period += (med - _period) * kPeriodGain;
                _retempoVotes = 0;
            }
            else if(++_retempoVotes >= kRetempoVotes)
            {
                // the music actually changed tempo; start over at the new one
                if(kDebugAudio) Serial.printf("RETEMPO %5.1f -> %5.1fbpm\n", bpm(), 60.0f / med);
                _period = med;
                _confidence = 0.3f;
                _retempoVotes = 0;
            }
        }

        // Phase evidence: an onset near a predicted beat pulls the grid into
        // lock. Detection runs kOnsetLatency behind the actual sound (attack
        // ramp, chunking, queueing), so onsets are expected to land that far
        // AFTER the gridline - which parks the gridline itself, and everything
        // driven by it, on the true beat.
        float expected = kOnsetLatency / _period;
        float raw = _beatPhase - expected;
        float err = raw - roundf(raw); // signed beats from the expected landing spot
        if(fabsf(err) < kPhaseWindow)
        {
            _beatPhase -= err * kPhaseGain;
            _totalBeats -= err * kPhaseGain;
            if(_beatPhase < 0) _beatPhase += 1.0f;
            if(_confidence > kConfidentThreshold)
            {
                // Onsets consistently landing on the same side of the gridline
                // mean the period itself is off; without this, a wrong period
                // can chase the phase forever while staying "confident"
                _period = std::min(1.2f, std::max(0.2f, _period * (1.0f + err * kFreqGain)));
            }
        }
        // Confidence only rewards onsets much closer to the grid than chance
        // (the reward window covers 30% of the beat), so sporadic room noise
        // nets negative and can't keep a phantom grid pulsing.
        if(fabsf(err) < kConfidenceWindow)
        {
            _confidence = std::min(1.0f, _confidence + 0.15f * (1.0f - fabsf(err) / kConfidenceWindow));
        }
        else
        {
            _confidence = std::max(0.0f, _confidence - 0.05f);
        }
    }

    float ioiEstimate()
    {
        // Detection quantizes to frame boundaries, holding a few ms off for
        // stretches before snapping back, so the distribution is lopsided and
        // a median lands on the wrong side. Whole-beat interpretation has
        // already filtered outliers, so the mean is safe and unbiased.
        float sum = 0;
        for(int i = 0; i < _ioiCount; i++) sum += _iois[i];
        return sum / _ioiCount;
    }

    bool _running = false;
    int16_t _chunks[2][kChunkSamples];
    int _nextChunk = 0;

    float _bass = 0;
    float _ring[kRingSize] = {0};
    int _ringIndex = 0;
    int _chunksSinceOnset = 0;
    float _lastEnergy = 0, _meanEnergy = 0;
    float _envelope = 0;

    TimeInterval _now = 0;
    TimeInterval _lastOnsetAt = -10;
    float _period = 0.5f;      // seconds per beat; 120bpm until told otherwise
    float _beatPhase = 0;      // 0..1 within the current beat
    TimeInterval _totalBeats = 0; // double: float would lose beat fractions within a day
    float _confidence = 0;
    float _iois[kIOIRingSize] = {0};
    int _ioiIndex = 0, _ioiCount = 0, _retempoVotes = 0;
    float _lastRawIoi = 0;
    int _steadyCount = 0;

    TimeInterval _sinceDebugPrint = 0;
};

extern BeatDetector beats;

#endif
