#ifndef __BEAT_DETECTOR__H
#define __BEAT_DETECTOR__H
#include "M5Unified.h"
#include <OverAnimate.h>
#include "Util.h"

// Beat detection from an M5Atom Echo's PDM microphone (SPM1423, CLK on G33,
// DATA on G23), captured through M5Unified's Mic_Class. Mic_Class selects PDM
// mode automatically when pin_bck is unset.
//
// Three cooperating layers:
//
// Bands: each 16ms chunk is split into a kick band (two cascaded one-poles,
// ~130Hz) and a hat/snare band (signal minus a ~1kHz one-pole). Each band's
// flux - the half-wave-rectified energy increase, normalized by that band's
// own running level so genres auto-balance - feeds an onset-strength envelope.
//
// Tempo: every ~0.5s the envelope of the last ~4s is autocorrelated over
// 60-180bpm lags, with harmonic comb reinforcement and parabolic sub-chunk
// refinement. Autocorrelation finds the pulse through syncopation and offbeat
// hats, where pairwise onset intervals get captured by them. A candidate that
// keeps disagreeing with the current period wins a retempo.
//
// Phase: discrete onsets are detected in the kick band only (hats sit on
// ambiguous half-beats, so they get no phase authority). Onsets near a
// predicted gridline pull the grid into lock and raise confidence; once
// confident, the grid fires the envelope instead of raw onsets - steady
// through syncopation - and keeps ticking through breakdowns, with confidence
// decaying until onsets return.
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

private:
    static const int kSampleRate = 16000;
    static const int kChunkSamples = 256;            // 16ms per decision
    static const int kRingSize = 48;                 // ~0.8s of kick-energy history
    static const int kRefractoryChunks = 9;          // >=144ms between onsets; at 124bpm a 256ms
                                                     // refractory shadowed the offbeat's competing event forever
    static constexpr float kKickAlpha = 0.050f;      // cascaded one-poles at ~130Hz
    static constexpr float kHighAlpha = 0.325f;      // ~1kHz one-pole; high band = signal minus this
    static constexpr float kSensitivity = 1.8f;      // onset = mean + this many std devs
    static constexpr float kEnergyFloor = 120.0f * 120.0f; // ignore near-silence; quiet room ambient is ~40 rms
    static constexpr float kEnvelopeDecay = 0.15f;   // seconds

    // tempo: autocorrelation of the flux envelope. The envelope is indexed by
    // wall-clock slots, not by chunk count: the mic queue is only 2 chunks
    // deep, so a slow frame silently drops audio, and a chunk-counted timeline
    // both compresses (breaking lag->seconds) and drifts with the PDM clock's
    // imperfect dividers (measured ~7% off nominal). Wall slots make lost
    // audio an honest zero-gap and lag->seconds exact by construction.
    static constexpr float kSlotSeconds = 0.016f;    // envelope resolution
    static const int kFluxRing = 256;                // ~4.1s of onset strength
    static const int kAcorrInterval = 32;            // evaluate every ~0.5s
    static const int kMinLag = 21;                   // 0.34s = ~178bpm
    static const int kMaxLag = 62;                   // 0.99s = ~60bpm
    static constexpr float kFluxMeanAlpha = 0.008f;  // ~2s running level per band
    static constexpr float kAcorrMinStrength = 1.5f; // peak/mean ratio to trust a candidate
    static const int kRetempoEvals = 2;              // consecutive disagreeing evaluations before snapping
    static constexpr float kPeriodGain = 0.3f;       // glide toward an agreeing candidate

    static constexpr float kPhaseWindow = 0.35f;     // beats: onsets this close to the grid pull phase
    static constexpr float kConfidenceWindow = 0.15f; // beats: only this close rewards confidence
    static constexpr float kPhaseGain = 0.5f;        // fraction of phase error corrected per onset
    static constexpr float kFreqGain = 0.05f;        // period correction from phase error (2nd PLL order)
    static constexpr float kConfidentThreshold = 0.4f; // above this, the grid drives the envelope
    static constexpr float kConfidenceDecay = 20.0f; // seconds; the grid outlives a breakdown
    static constexpr float kOnsetLatency = 0.00f;    // seconds detection lags the sound; tune by eye against a metronome

    static const bool kDebugAudio = true;            // serial meter for bring-up

    void analyze(const int16_t *samples)
    {
        float kickEnergy = 0, highEnergy = 0;
        for(int i = 0; i < kChunkSamples; i++)
        {
            _lp1 += kKickAlpha * (samples[i] - _lp1);
            _lp2 += kKickAlpha * (_lp1 - _lp2);
            kickEnergy += _lp2 * _lp2;
            _lpHigh += kHighAlpha * (samples[i] - _lpHigh);
            float high = samples[i] - _lpHigh;
            highEnergy += high * high;
        }
        kickEnergy /= kChunkSamples;
        highEnergy /= kChunkSamples;
        _lastEnergy = kickEnergy;

        // Flux per band, normalized by the band's own running level, summed
        // into the onset-strength envelope the autocorrelation runs on
        float kickFlux = std::max(0.0f, kickEnergy - _prevKickEnergy);
        float highFlux = std::max(0.0f, highEnergy - _prevHighEnergy);
        _prevKickEnergy = kickEnergy;
        _prevHighEnergy = highEnergy;
        _kickFluxMean += kFluxMeanAlpha * (kickFlux - _kickFluxMean);
        _highFluxMean += kFluxMeanAlpha * (highFlux - _highFluxMean);
        float strength = kickFlux / (_kickFluxMean + 1.0f) + highFlux / (_highFluxMean + 1.0f);
        long slot = (long)(_now / kSlotSeconds);
        if(_lastSlot == 0) _lastSlot = slot - 1;
        while(_lastSlot < slot)
        {
            _lastSlot++;
            _flux[_lastSlot % kFluxRing] = 0;
            if(++_slotsSinceAcorr >= kAcorrInterval)
            {
                _slotsSinceAcorr = 0;
                evaluateTempo();
            }
        }
        _flux[slot % kFluxRing] += strength; // two chunks in one slot both count

        // Discrete onsets trigger on kick-band FLUX, not level: an offbeat
        // bass stab carries more sustained low-frequency energy than the kick
        // (the minimal-techno signature, worsened by sidechain pumping), and a
        // level detector anti-phase locks the whole grid onto it. The kick's
        // rise is sharper; flux picks the transient over the swell.
        _ring[_ringIndex] = kickFlux;
        _ringIndex = (_ringIndex + 1) % kRingSize;
        float mean = 0;
        for(int i = 0; i < kRingSize; i++) mean += _ring[i];
        mean /= kRingSize;
        float var = 0;
        for(int i = 0; i < kRingSize; i++) var += (_ring[i] - mean) * (_ring[i] - mean);
        var /= kRingSize;
        _meanEnergy = mean;

        // Attack concentration is the onset's phase authority: a kick's rise
        // fits in one chunk, a bass swell's spreads over several, so kicks
        // out-vote offbeat stabs without needing perfect classification. The
        // first chunk of ANY rise looks sharp, so a candidate pends for 3 more
        // chunks to see its whole attack before being judged; the constant
        // ~48ms delay folds into kOnsetLatency.
        _chunksSinceOnset++;
        if(_pendingChunks < 0
            && kickEnergy > kEnergyFloor
            && kickFlux > mean + kSensitivity * sqrtf(var)
            && _chunksSinceOnset >= kRefractoryChunks)
        {
            _pendingChunks = 3;
            _pendingBase = kickEnergy - kickFlux; // level just before the rise
            _pendingMaxFlux = kickFlux;
            _pendingMaxEnergy = kickEnergy;
            _pendingPhase = _beatPhase;
        }
        else if(_pendingChunks >= 0)
        {
            _pendingMaxFlux = std::max(_pendingMaxFlux, kickFlux);
            _pendingMaxEnergy = std::max(_pendingMaxEnergy, kickEnergy);
            if(--_pendingChunks < 0)
            {
                float totalRise = std::max(_pendingMaxEnergy - _pendingBase, 1.0f);
                float sharpness = std::min(1.0f, _pendingMaxFlux / totalRise);
                _chunksSinceOnset = 0;
                if(kDebugAudio) Serial.printf("ONSET rms %6.0f sharp %.2f phase %.2f\n", sqrtf(_pendingMaxEnergy), sharpness, _pendingPhase);
                onKickOnset(sharpness, _pendingPhase);
            }
        }
    }

    // Autocorrelate the flux envelope over one tempo octave. Comb
    // reinforcement (score += half the 2x-lag correlation) favors the beat
    // over its subdivisions; near-ties prefer the longer lag for the same
    // reason. Parabolic interpolation refines the peak below chunk size.
    void evaluateTempo()
    {
        static float centered[kFluxRing];
        float mean = 0;
        for(int i = 0; i < kFluxRing; i++) mean += _flux[i];
        mean /= kFluxRing;
        // oldest slot first, so lags index backwards in time coherently
        for(int i = 0; i < kFluxRing; i++)
        {
            centered[i] = _flux[(_lastSlot + 1 + i) % kFluxRing] - mean;
        }
        // Smear the single-slot flux spikes: beat periods are rarely a whole
        // number of slots, so unsmoothed spikes miss alignment at integer lags
        // (observed as a strong preference for even-multiple lags at 140bpm)
        static float smoothed[kFluxRing];
        smoothed[0] = centered[0];
        smoothed[kFluxRing - 1] = centered[kFluxRing - 1];
        for(int i = 1; i < kFluxRing - 1; i++)
        {
            smoothed[i] = 0.25f * centered[i-1] + 0.5f * centered[i] + 0.25f * centered[i+1];
        }

        static float r[2 * kMaxLag + 1];
        for(int lag = kMinLag; lag <= 2 * kMaxLag; lag++)
        {
            float sum = 0;
            for(int i = lag; i < kFluxRing; i++) sum += smoothed[i] * smoothed[i - lag];
            r[lag] = sum / (kFluxRing - lag);
        }

        // A log-Gaussian tempo prior centered on 120bpm breaks octave ties:
        // for a uniform beat train r(P) equals r(2P), so without a prior the
        // beat/half-tempo choice is a coin flip.
        static float prior[kMaxLag + 1] = {0};
        if(prior[kMinLag] == 0)
        {
            for(int lag = kMinLag; lag <= kMaxLag; lag++)
            {
                float octaves = log2f(lag * kSlotSeconds / 0.5f) / 0.9f;
                prior[lag] = expf(-0.5f * octaves * octaves);
            }
        }

        // The comb (score += half the 2x-lag correlation) prefers the beat
        // over its subdivisions: an 8th's double is the beat, but the beat's
        // double is the also-strong 2-beat lag.
        int bestLag = kMinLag;
        float bestScore = -1e30f;
        float scoreSum = 0;
        for(int lag = kMinLag; lag <= kMaxLag; lag++)
        {
            float score = (r[lag] + 0.5f * r[2 * lag]) * prior[lag];
            scoreSum += std::max(0.0f, score);
            if(score > bestScore) { bestScore = score; bestLag = lag; }
        }
        float scoreMean = scoreSum / (kMaxLag - kMinLag + 1);
        if(scoreMean <= 1e-9f || bestScore < kAcorrMinStrength * scoreMean)
        {
            _retempoEvals = 0; // nothing rhythmic enough to act on
            return;
        }

        // parabolic peak refinement on the raw autocorrelation
        float refined = bestLag;
        if(bestLag > kMinLag && bestLag < kMaxLag)
        {
            float y0 = r[bestLag - 1], y1 = r[bestLag], y2 = r[bestLag + 1];
            float denom = y0 - 2 * y1 + y2;
            if(fabsf(denom) > 1e-9f) refined += 0.5f * (y0 - y2) / denom;
        }
        float candidate = refined * kSlotSeconds;

        if(kDebugAudio)
        {
            int top[3] = {0, 0, 0};
            float topScore[3] = {-1e30f, -1e30f, -1e30f};
            for(int lag = kMinLag; lag <= kMaxLag; lag++)
            {
                float score = (r[lag] + 0.5f * r[2 * lag]) * prior[lag];
                for(int t = 0; t < 3; t++)
                {
                    if(score > topScore[t])
                    {
                        for(int u = 2; u > t; u--) { top[u] = top[u-1]; topScore[u] = topScore[u-1]; }
                        top[t] = lag; topScore[t] = score;
                        break;
                    }
                }
            }
            Serial.printf("ACORR %5.1fbpm (lag %.1f) str %.1f | top %d:%.0f %d:%.0f %d:%.0f\n",
                          60.0f / candidate, refined, bestScore / scoreMean,
                          top[0], topScore[0], top[1], topScore[1], top[2], topScore[2]);
        }

        if(fabsf(candidate - _period) / _period < 0.1f)
        {
            _period += (candidate - _period) * kPeriodGain;
            _retempoEvals = 0;
        }
        else if(++_retempoEvals >= kRetempoEvals)
        {
            if(kDebugAudio) Serial.printf("RETEMPO %5.1f -> %5.1fbpm\n", bpm(), 60.0f / candidate);
            _period = candidate;
            _confidence = std::min(_confidence, 0.3f);
            _retempoEvals = 0;
        }
    }

    void onKickOnset(float sharpness, float phaseAtOnset)
    {
        // Until the grid is trustworthy, raw onsets drive the visuals
        if(_confidence <= kConfidentThreshold) _envelope = 1.0f;

        // Phase evidence: an onset near a predicted beat pulls the grid into
        // lock. Detection runs kOnsetLatency behind the actual sound (attack
        // ramp, chunking, queueing), so onsets are expected to land that far
        // AFTER the gridline - which parks the gridline itself, and everything
        // driven by it, on the true beat.
        float expected = kOnsetLatency / _period;
        float raw = phaseAtOnset - expected;
        float err = raw - roundf(raw); // signed beats from the expected landing spot
        if(fabsf(err) < kPhaseWindow)
        {
            _offGridStreak = 0;
            _beatPhase -= err * kPhaseGain * sharpness;
            _totalBeats -= err * kPhaseGain * sharpness;
            if(_beatPhase < 0) _beatPhase += 1.0f;
            if(_confidence > kConfidentThreshold)
            {
                // Onsets consistently landing on the same side of the gridline
                // mean the period itself is off; without this, a wrong period
                // can chase the phase forever while staying "confident"
                _period = std::min(1.2f, std::max(0.2f, _period * (1.0f + err * kFreqGain * sharpness)));
            }
        }
        else if(sharpness > 0.6f && ++_offGridStreak >= 4)
        {
            // A run of sharp onsets the pull window can't reach means the
            // grid, not the music, is wrong - a half-beat-offset lock is
            // otherwise a stable fixpoint (kicks at phase 0.5 forever,
            // confidence starved). Soft onsets don't count: an offbeat bass
            // swell must not re-anchor the grid onto itself.
            _offGridStreak = 0;
            if(kDebugAudio) Serial.printf("REPHASE err %.2f\n", err);
            _beatPhase -= err;
            _totalBeats -= err;
            if(_beatPhase < 0) _beatPhase += 1.0f;
            if(_beatPhase >= 1.0f) _beatPhase -= 1.0f;
        }
        // Confidence only rewards onsets much closer to the grid than chance
        // (the reward window covers 30% of the beat), so sporadic room noise
        // nets negative and can't keep a phantom grid pulsing. Both reward and
        // penalty scale with sharpness: soft events shouldn't build a lock or
        // drain one.
        if(fabsf(err) < kConfidenceWindow)
        {
            _confidence = std::min(1.0f, _confidence + 0.15f * sharpness * (1.0f - fabsf(err) / kConfidenceWindow));
        }
        else
        {
            _confidence = std::max(0.0f, _confidence - 0.05f * sharpness);
        }
    }

    bool _running = false;
    int16_t _chunks[2][kChunkSamples];
    int _nextChunk = 0;

    float _lp1 = 0, _lp2 = 0, _lpHigh = 0;
    float _prevKickEnergy = 0, _prevHighEnergy = 0;
    float _kickFluxMean = 0, _highFluxMean = 0;
    float _flux[kFluxRing] = {0};
    long _lastSlot = 0;
    int _slotsSinceAcorr = 0;
    int _retempoEvals = 0;

    float _ring[kRingSize] = {0};
    int _ringIndex = 0;
    int _chunksSinceOnset = 0;
    int _offGridStreak = 0;
    int _pendingChunks = -1;   // -1: no onset candidate in flight
    float _pendingBase = 0, _pendingMaxFlux = 0, _pendingMaxEnergy = 0;
    float _pendingPhase = 0;
    float _lastEnergy = 0, _meanEnergy = 0;
    float _envelope = 0;

    TimeInterval _now = 0;
    float _period = 0.5f;      // seconds per beat; 120bpm until told otherwise
    float _beatPhase = 0;      // 0..1 within the current beat
    float _confidence = 0;
    TimeInterval _totalBeats = 0; // double: float would lose beat fractions within a day

    TimeInterval _sinceDebugPrint = 0;
};

extern BeatDetector beats;

#endif
