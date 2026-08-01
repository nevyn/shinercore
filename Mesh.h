#ifndef __MESH__H
#define __MESH__H
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <OverAnimate.h>
#include "ShinyTypes.h"
#include "Codecs.h"
#include "BeatDetector.h"
#include "Util.h"

// Device-to-device sync over ESP-NOW: connectionless 250-byte broadcasts on a
// fixed wifi channel, no pairing, works across ESP32 and ESP32-S3. Coexists
// with the BLE app protocol on the shared 2.4GHz radio via IDF's arbiter.
//
// All frames are broadcast; there is no addressing and no session. Everything
// must stay correct with lost frames and repeated frames.

#define kMeshChannel 1
#define kMeshVersion 1

static const uint8_t kBroadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum MeshFrameType : uint8_t
{
    MeshFrameBeat = 1,
    MeshFramePreset = 2,
};

#define kMeshFlagHasMic 1
#define kMeshFlagConfident 2
#define kMeshFlagFollowing 4 // this grid is itself relayed from a leader

struct __attribute__((packed)) MeshBeatFrame
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;      // kMeshFlag*
    float period;       // seconds per beat
    float phase;        // 0..1 within the current beat, at send time
    double beatTime;    // fractional beats since sender's boot
    float confidence;   // 0..1
    uint8_t color[3];   // sender's layer 0 mainColor
};

// A neighbor's current preset, one non-empty layer per entry. Ten of these
// plus the header still fit one 250-byte ESP-NOW frame.
struct __attribute__((packed)) MeshPresetLayer
{
    uint8_t layer;
    uint8_t animation;  // index into animationNames; only valid same-version
    uint8_t blendMode;
    uint8_t beatSync;
    uint8_t color1[3];
    uint8_t color2[3];
    float speed;
    float tau;
    float phi;
};

struct __attribute__((packed)) MeshPresetFrame
{
    uint8_t version;
    uint8_t type;
    uint8_t layerCount;
    MeshPresetLayer layers[LAYER_COUNT]; // only layerCount are sent
};

struct MeshNeighbor
{
    bool used = false;
    uint8_t mac[6];
    TimeInterval sinceBeat = 0;
    MeshBeatFrame beat = {};
    bool hasPreset = false;
    ShinyLayerSettings preset[LAYER_COUNT]; // decoded; unsent layers stay default (Nothing)
};

class Mesh
{
public:
    void begin()
    {
        if(_running) return;
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();  // no AP: park on our fixed channel
        esp_wifi_set_channel(kMeshChannel, WIFI_SECOND_CHAN_NONE);
        if(esp_now_init() != ESP_OK)
        {
            logger.println("mesh: esp_now_init failed");
            WiFi.mode(WIFI_OFF);
            return;
        }
        esp_now_register_send_cb(&Mesh::onSent);
        esp_now_register_recv_cb(&Mesh::onReceived);

        esp_now_peer_info_t broadcast = {};
        memset(broadcast.peer_addr, 0xFF, 6);
        broadcast.channel = kMeshChannel;
        broadcast.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&broadcast);

        esp_wifi_get_mac(WIFI_IF_STA, _mac);
        _running = true;
        logger.print("mesh: on, "); logger.println(WiFi.macAddress());
    }

    void end()
    {
        if(!_running) return;
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
        _running = false;
        logger.println("mesh: off");
    }

    bool running() const { return _running; }

    void update(TimeInterval delta)
    {
        if(!_running) return;

        // drain frames the wifi task queued
        while(_rxTail != _rxHead)
        {
            RxFrame &frame = _rxRing[_rxTail];
            handleFrame(frame.mac, frame.data, frame.len);
            _rxTail = (_rxTail + 1) % kRxRingSize;
        }

        // age out neighbors we stopped hearing
        for(auto &n : _neighbors)
        {
            if(!n.used) continue;
            n.sinceBeat += delta;
            if(n.sinceBeat > kNeighborTimeout)
            {
                n.used = false;
                if(_following && memcmp(_leaderMac, n.mac, 6) == 0)
                {
                    _following = false;
                    if(kDebugMesh) Serial.printf("mesh: leader went quiet, own grid again\n");
                }
                if(kDebugMesh) Serial.printf("mesh: lost %s\n", macStr(n.mac));
            }
        }
        rebuildSlots();

        _sinceTx += delta;
        if(_sinceTx >= _txInterval)
        {
            _sinceTx = 0;
            _txInterval = 1.0 + (esp_random() % 250) / 1000.0; // jitter avoids beacon collisions
            sendBeat();
        }

        // Share our current preset: when it changes and has settled for 1s
        // (a slider drag becomes one frame), plus a slow refresh for latecomers
        _sincePresetTx += delta;
        if(memcmp(_seenPreset, localPrefs.layers, sizeof(_seenPreset)) != 0)
        {
            memcpy(_seenPreset, localPrefs.layers, sizeof(_seenPreset));
            _sincePresetChange = 0;
        }
        else _sincePresetChange += delta;
        bool unsent = memcmp(_sentPreset, localPrefs.layers, sizeof(_sentPreset)) != 0;
        if((unsent && _sincePresetChange > 1.0) || _sincePresetTx > 5.0)
        {
            sendPreset();
        }

        if(kDebugMesh)
        {
            _sinceDebugPrint += delta;
            if(_sinceDebugPrint > 5.0)
            {
                _sinceDebugPrint = 0;
                Serial.printf("mesh tx %lu ok %lu fail %lu | rx %lu drop %lu | %d neighbors\n",
                              _txCount, _txOkCount, _txFailCount, _rxCount, _rxDropCount, neighborCount());
            }
        }
    }

    int neighborCount() const
    {
        int count = 0;
        for(const auto &n : _neighbors) count += n.used;
        return count;
    }

    // The carousel: with meshShow on, every core plays every core's current
    // preset, carouselBeats each, switching in lockstep because the slot
    // number derives from the shared beat grid. Layers cross over one at a
    // time across the first kCarouselFadeBeats of a slot (deterministically
    // random per slot, same on every core), overlapping into a tween as the
    // rendered settings slew to each new target.
    const ShinyLayerSettings *carouselLayers(int layer)
    {
        if(!_running || !localPrefs.meshShow || _slotCount <= 1) return &localPrefs.layers[layer];

        double bt = beats.beatTime();
        if(bt < 0) bt = 0;
        int per = localPrefs.carouselBeats < 1 ? 1 : localPrefs.carouselBeats;
        long long slotNumber = (long long)(bt / per);
        double intoSlot = bt - (double)slotNumber * per;
        float threshold = (mix(layer * 7919u + (uint32_t)slotNumber) % 1000) / 1000.0f * kCarouselFadeBeats;
        long long effective = intoSlot >= threshold ? slotNumber : slotNumber - 1;
        const Slot &slot = _slots[(int)(((effective % _slotCount) + _slotCount) % _slotCount)];
        return &slot.layers[layer];
    }

private:
    static const bool kDebugMesh = true; // serial counters for bring-up

    void sendBeat()
    {
        MeshBeatFrame frame = {};
        frame.version = kMeshVersion;
        frame.type = MeshFrameBeat;
        frame.flags = (localPrefs.micEnabled ? kMeshFlagHasMic : 0)
                    | (beats.confidence() > 0.4f ? kMeshFlagConfident : 0)
                    | (_following ? kMeshFlagFollowing : 0);
        frame.period = beats.period();
        frame.phase = beats.phase();
        frame.beatTime = beats.beatTime();
        frame.confidence = beats.confidence();
        frame.color[0] = localPrefs.layers[0].mainColor.r;
        frame.color[1] = localPrefs.layers[0].mainColor.g;
        frame.color[2] = localPrefs.layers[0].mainColor.b;

        _txCount++;
        if(esp_now_send(kBroadcastAddr, (const uint8_t*)&frame, sizeof(frame)) != ESP_OK)
        {
            _txFailCount++;
        }
    }

    void sendPreset()
    {
        MeshPresetFrame frame = {};
        frame.version = kMeshVersion;
        frame.type = MeshFramePreset;
        for(int i = 0; i < LAYER_COUNT; i++)
        {
            const ShinyLayerSettings &l = localPrefs.layers[i];
            if(l.animationIndex == 0) continue; // Nothing
            MeshPresetLayer &out = frame.layers[frame.layerCount++];
            out.layer = i;
            out.animation = l.animationIndex;
            out.blendMode = l.blendMode;
            out.beatSync = l.beatSync;
            out.color1[0] = l.mainColor.r; out.color1[1] = l.mainColor.g; out.color1[2] = l.mainColor.b;
            out.color2[0] = l.secondaryColor.r; out.color2[1] = l.secondaryColor.g; out.color2[2] = l.secondaryColor.b;
            out.speed = l.speed;
            out.tau = l.p_tau;
            out.phi = l.p_phi;
        }
        size_t size = offsetof(MeshPresetFrame, layers) + frame.layerCount * sizeof(MeshPresetLayer);
        memcpy(_sentPreset, localPrefs.layers, sizeof(_sentPreset));
        _sincePresetTx = 0;
        _txCount++;
        if(esp_now_send(kBroadcastAddr, (const uint8_t*)&frame, size) != ESP_OK)
        {
            _txFailCount++;
        }
    }

    void handleFrame(const uint8_t *mac, const uint8_t *data, int len)
    {
        if(len < 2 || data[0] != kMeshVersion) return;
        switch(data[1])
        {
            case MeshFrameBeat:
                if(len == sizeof(MeshBeatFrame)) handleBeat(mac, *(const MeshBeatFrame*)data);
                break;
            case MeshFramePreset:
                handlePreset(mac, data, len);
                break;
        }
    }

    void handlePreset(const uint8_t *mac, const uint8_t *data, int len)
    {
        const MeshPresetFrame *frame = (const MeshPresetFrame*)data;
        if(len < (int)offsetof(MeshPresetFrame, layers)) return;
        if(frame->layerCount > LAYER_COUNT) return;
        if(len != (int)(offsetof(MeshPresetFrame, layers) + frame->layerCount * sizeof(MeshPresetLayer))) return;

        MeshNeighbor *n = upsertNeighbor(mac);
        if(!n) return;

        // decode with hostile-input clamps: anyone can broadcast ESP-NOW frames
        for(int i = 0; i < LAYER_COUNT; i++) n->preset[i] = ShinyLayerSettings();
        for(int i = 0; i < frame->layerCount; i++)
        {
            const MeshPresetLayer &in = frame->layers[i];
            if(in.layer >= LAYER_COUNT) continue;
            ShinyLayerSettings &l = n->preset[in.layer];
            l.animationIndex = in.animation < (int)animationNames.size() ? in.animation : 0;
            l.blendMode = in.blendMode < BlendModeCount ? (LayerBlendMode)in.blendMode : BlendModeAdd;
            l.beatSync = in.beatSync ? 1 : 0;
            l.mainColor = CRGB(in.color1[0], in.color1[1], in.color1[2]);
            l.secondaryColor = CRGB(in.color2[0], in.color2[1], in.color2[2]);
            l.speed = isfinite(in.speed) ? clampTo(in.speed, 0.001f, 100.0f) : 1.0f;
            l.p_tau = isfinite(in.tau) ? clampTo(in.tau, -100.0f, 100.0f) : 10.0f;
            l.p_phi = isfinite(in.phi) ? clampTo(in.phi, -100.0f, 100.0f) : 4.0f;
        }
        n->hasPreset = true;
    }

    void handleBeat(const uint8_t *mac, const MeshBeatFrame &beat)
    {
        MeshNeighbor *n = upsertNeighbor(mac);
        if(!n) return; // table full of fresher neighbors
        n->beat = beat;
        n->sinceBeat = 0;
        considerFollowing(*n);
    }

    struct Rank
    {
        float conf;
        bool following; // relayed grids lose ties, which makes follow-cycles impossible
        bool mic;
        const uint8_t *mac;
    };
    Rank rankSelf() const { return {beats.confidence(), false, (bool)localPrefs.micEnabled, _mac}; }
    static Rank rankOf(const MeshNeighbor &n)
    {
        return {n.beat.confidence, (bool)(n.beat.flags & kMeshFlagFollowing),
                (bool)(n.beat.flags & kMeshFlagHasMic), n.mac};
    }

    // a outranks b when clearly more confident, or comparably confident and
    // better placed: own grid beats a relay, a mic beats no mic, and the
    // lowest MAC is the stable tiebreak
    static bool outranks(const Rank &a, const Rank &b)
    {
        if(a.conf > b.conf + 0.1f) return true;
        if(b.conf > a.conf + 0.1f) return false;
        if(a.following != b.following) return b.following;
        if(a.mic != b.mic) return a.mic;
        return memcmp(a.mac, b.mac, 6) < 0;
    }

    // Follow the best grid in earshot. The current leader keeps our grid
    // pulled to theirs on every beacon; anyone else (including ourselves)
    // must outrank the incumbent to take over.
    void considerFollowing(const MeshNeighbor &n)
    {
        if(!(n.beat.flags & kMeshFlagConfident)) return;

        bool isLeader = _following && memcmp(_leaderMac, n.mac, 6) == 0;
        if(isLeader && outranks(rankSelf(), rankOf(n)))
        {
            _following = false;
            if(kDebugMesh) Serial.printf("mesh: outrank %s, own grid again\n", macStr(n.mac));
            return;
        }
        if(!isLeader)
        {
            Rank incumbent = rankSelf();
            if(_following)
            {
                for(const auto &l : _neighbors)
                {
                    if(l.used && memcmp(l.mac, _leaderMac, 6) == 0) { incumbent = rankOf(l); break; }
                }
            }
            if(!outranks(rankOf(n), incumbent)) return;
            _following = true;
            memcpy(_leaderMac, n.mac, 6);
            if(kDebugMesh) Serial.printf("mesh: following %s\n", macStr(n.mac));
        }
        beats.applyNetworkBeat(n.beat.period, n.beat.phase, n.beat.beatTime, n.beat.confidence);
    }

    MeshNeighbor *upsertNeighbor(const uint8_t *mac)
    {
        MeshNeighbor *free_ = nullptr, *oldest = nullptr;
        for(auto &n : _neighbors)
        {
            if(n.used && memcmp(n.mac, mac, 6) == 0) return &n;
            if(!n.used) free_ = &n;
            else if(!oldest || n.sinceBeat > oldest->sinceBeat) oldest = &n;
        }
        MeshNeighbor *slot = free_ ? free_ : (oldest && oldest->sinceBeat > 2.0 ? oldest : nullptr);
        if(slot)
        {
            slot->used = true;
            memcpy(slot->mac, mac, 6);
            if(kDebugMesh) Serial.printf("mesh: hello %s\n", macStr(mac));
        }
        return slot;
    }

    struct Slot
    {
        const ShinyLayerSettings *layers;
        const uint8_t *mac;
    };
    static constexpr float kCarouselFadeBeats = 2.0f; // layers cross over within this window

    void rebuildSlots()
    {
        _slotCount = 0;
        _slots[_slotCount++] = {localPrefs.layers, _mac};
        for(auto &n : _neighbors)
        {
            if(n.used && n.hasPreset) _slots[_slotCount++] = {n.preset, n.mac};
        }
        // sorted by mac so every core agrees on the running order
        for(int i = 1; i < _slotCount; i++)
        {
            for(int j = i; j > 0 && memcmp(_slots[j].mac, _slots[j-1].mac, 6) < 0; j--)
            {
                std::swap(_slots[j], _slots[j-1]);
            }
        }
    }

    static uint32_t mix(uint32_t x)
    {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        return (x >> 16) ^ x;
    }

    static const char *macStr(const uint8_t *mac)
    {
        static char buf[18];
        snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return buf;
    }

    // Both callbacks run in the wifi task: only counters and the SPSC ring
    // here; real handling happens in update() on the loop task.
    static void onSent(const uint8_t *mac, esp_now_send_status_t status)
    {
        if(status == ESP_NOW_SEND_SUCCESS) _instance->_txOkCount++;
        else _instance->_txFailCount++;
    }
    static void onReceived(const uint8_t *mac, const uint8_t *data, int len)
    {
        _instance->_rxCount++;
        int head = _instance->_rxHead;
        int next = (head + 1) % kRxRingSize;
        if(next == _instance->_rxTail || len > (int)sizeof(RxFrame::data))
        {
            _instance->_rxDropCount++;
            return;
        }
        RxFrame &slot = _instance->_rxRing[head];
        memcpy(slot.mac, mac, 6);
        slot.len = len;
        memcpy(slot.data, data, len);
        _instance->_rxHead = next;
    }

    static const int kRxRingSize = 8;
    static const int kMaxNeighbors = 8;
    static constexpr float kNeighborTimeout = 5.0f; // seconds without a beacon

    struct RxFrame
    {
        uint8_t mac[6];
        uint8_t len;
        uint8_t data[250]; // ESP-NOW max payload
    };

    static Mesh *_instance;
    bool _running = false;
    uint8_t _mac[6] = {0};
    Slot _slots[1 + kMaxNeighbors];
    int _slotCount = 0;
    ShinyLayerSettings _seenPreset[LAYER_COUNT];
    ShinyLayerSettings _sentPreset[LAYER_COUNT];
    TimeInterval _sincePresetChange = 0;
    TimeInterval _sincePresetTx = 0;
    bool _following = false;
    uint8_t _leaderMac[6] = {0};
    RxFrame _rxRing[kRxRingSize];
    volatile int _rxHead = 0; // written by wifi task
    volatile int _rxTail = 0; // written by loop task
    MeshNeighbor _neighbors[kMaxNeighbors];
    TimeInterval _sinceTx = 0;
    TimeInterval _txInterval = 0;
    TimeInterval _sinceDebugPrint = 0;
    unsigned long _txCount = 0, _txOkCount = 0, _txFailCount = 0, _rxCount = 0, _rxDropCount = 0;

public:
    Mesh() { _instance = this; }
};

#endif
