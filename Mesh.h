#ifndef __MESH__H
#define __MESH__H
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <OverAnimate.h>
#include "ShinyTypes.h"
#include "Util.h"

// Device-to-device sync over ESP-NOW: connectionless 250-byte broadcasts on a
// fixed wifi channel, no pairing, works across ESP32 and ESP32-S3. Coexists
// with the BLE app protocol on the shared 2.4GHz radio via IDF's arbiter.
//
// All frames are broadcast; there is no addressing and no session. Everything
// must stay correct with lost frames and repeated frames.

#define kMeshChannel 1
#define kMeshVersion 1

enum MeshFrameType : uint8_t
{
    MeshFrameBeat = 1,
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

struct MeshNeighbor
{
    bool used = false;
    uint8_t mac[6];
    TimeInterval sinceBeat = 0;
    MeshBeatFrame beat = {};
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

        _sinceTx += delta;
        if(_sinceTx >= _txInterval)
        {
            _sinceTx = 0;
            _txInterval = 1.0 + (esp_random() % 250) / 1000.0; // jitter avoids beacon collisions
            sendBeat();
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

        static const uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        _txCount++;
        if(esp_now_send(broadcastAddr, (const uint8_t*)&frame, sizeof(frame)) != ESP_OK)
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
        }
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
