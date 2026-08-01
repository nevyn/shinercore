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

struct __attribute__((packed)) MeshBeatFrame
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;      // bit 0: has a mic; bit 1: grid is confident
    float period;       // seconds per beat
    float phase;        // 0..1 within the current beat, at send time
    double beatTime;    // fractional beats since sender's boot
    float confidence;   // 0..1
    uint8_t color[3];   // sender's layer 0 mainColor
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
                Serial.printf("mesh tx %lu ok %lu fail %lu | rx %lu\n", _txCount, _txOkCount, _txFailCount, _rxCount);
            }
        }
    }

private:
    static const bool kDebugMesh = true; // serial counters for bring-up

    void sendBeat()
    {
        MeshBeatFrame frame = {};
        frame.version = kMeshVersion;
        frame.type = MeshFrameBeat;
        frame.flags = (localPrefs.micEnabled ? 1 : 0) | (beats.confidence() > 0.4f ? 2 : 0);
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

    // Both callbacks run in the wifi task: touch nothing but counters here.
    // Real payload handling copies into a ring drained from update().
    static void onSent(const uint8_t *mac, esp_now_send_status_t status)
    {
        if(status == ESP_NOW_SEND_SUCCESS) _instance->_txOkCount++;
        else _instance->_txFailCount++;
    }
    static void onReceived(const uint8_t *mac, const uint8_t *data, int len)
    {
        _instance->_rxCount++;
    }

    static Mesh *_instance;
    bool _running = false;
    TimeInterval _sinceTx = 0;
    TimeInterval _txInterval = 0;
    TimeInterval _sinceDebugPrint = 0;
    unsigned long _txCount = 0, _txOkCount = 0, _txFailCount = 0, _rxCount = 0;

public:
    Mesh() { _instance = this; }
};

#endif
