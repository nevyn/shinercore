#ifndef __BEAT_DETECTOR__H
#define __BEAT_DETECTOR__H
#include "M5Unified.h"
#include <OverAnimate.h>
#include "Util.h"

// Beat detection from an M5Atom Echo's PDM microphone (SPM1423, CLK on G33,
// DATA on G23), captured through M5Unified's Mic_Class. Mic_Class selects PDM
// mode automatically when pin_bck is unset.
//
// Detection is energy-based, no FFT: each 16ms chunk of audio is low-pass
// filtered to keep the kick-drum band, and its energy is compared against the
// mean and deviation of the last ~0.8s. An energy spike is a beat. envelope()
// then decays 1 -> 0 for animations to render.
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
        _envelope *= expf(-delta / kEnvelopeDecay);
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
                Serial.printf("mic rms %6.0f  avg %6.0f  envelope %.2f\n",
                              sqrtf(_lastEnergy), sqrtf(_meanEnergy), _envelope);
            }
        }
    }

    /// 1.0 at the moment of a beat, exponentially decaying to 0. Render this.
    float envelope() const { return _envelope; }

private:
    static const int kSampleRate = 16000;
    static const int kChunkSamples = 256;            // 16ms per decision
    static const int kRingSize = 48;                 // ~0.8s of energy history
    static const int kRefractoryChunks = 16;         // >=256ms between beats = <=234bpm
    static constexpr float kBassAlpha = 0.061f;      // one-pole LPF at ~160Hz
    static constexpr float kSensitivity = 1.8f;      // beat = mean + this many std devs. TUNE
    static constexpr float kEnergyFloor = 120.0f * 120.0f; // ignore near-silence; quiet room ambient is ~40 rms
    static constexpr float kEnvelopeDecay = 0.15f;   // seconds
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

        _chunksSinceBeat++;
        if(energy > kEnergyFloor
            && energy > mean + kSensitivity * sqrtf(var)
            && _chunksSinceBeat >= kRefractoryChunks)
        {
            _chunksSinceBeat = 0;
            _envelope = 1.0f;
            if(kDebugAudio) Serial.printf("BEAT  rms %6.0f over avg %6.0f\n", sqrtf(energy), sqrtf(mean));
        }
    }

    bool _running = false;
    int16_t _chunks[2][kChunkSamples];
    int _nextChunk = 0;

    float _bass = 0;
    float _ring[kRingSize] = {0};
    int _ringIndex = 0;
    int _chunksSinceBeat = 0;
    float _lastEnergy = 0, _meanEnergy = 0;
    float _envelope = 0;
    TimeInterval _sinceDebugPrint = 0;
};

extern BeatDetector beats;

#endif
