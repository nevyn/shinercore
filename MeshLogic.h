#ifndef __MESH_LOGIC__H
#define __MESH_LOGIC__H
#include <string.h>
#include <stdint.h>
#include "ShinyTypes.h"

// The pure half of the mesh: wire formats and decision math, no radio.
// Host-tested in test/meshtest.cpp; Mesh.h owns everything with side effects.

#define kMeshVersion 1
#define kMeshFlagHasMic 1
#define kMeshFlagConfident 2
#define kMeshFlagFollowing 4 // this grid is itself relayed from a leader

enum MeshFrameType : uint8_t
{
    MeshFrameBeat = 1,
    MeshFramePreset = 2,
};

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

struct MeshRank
{
    float conf;
    bool following; // relayed grids lose ties, which makes follow-cycles impossible
    bool mic;
    const uint8_t *mac;
};

// a outranks b when clearly more confident, or comparably confident and
// better placed: own grid beats a relay, a mic beats no mic, and the
// lowest MAC is the stable tiebreak
static inline bool meshOutranks(const MeshRank &a, const MeshRank &b)
{
    if(a.conf > b.conf + 0.1f) return true;
    if(b.conf > a.conf + 0.1f) return false;
    if(a.following != b.following) return b.following;
    if(a.mic != b.mic) return a.mic;
    return memcmp(a.mac, b.mac, 6) < 0;
}

static inline uint32_t meshMix(uint32_t x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    return (x >> 16) ^ x;
}

// Which carousel slot a layer should render at this beat time. The slot
// number derives from the shared grid, so synced cores agree; each layer
// crosses over at its own deterministically-random point within the first
// fadeBeats of a slot, so transitions overlap into a tween.
static inline int meshCarouselSlot(double beatTime, int beatsPerSlot, int slotCount, uint32_t layer, float fadeBeats)
{
    if(slotCount <= 1) return 0;
    if(beatTime < 0) beatTime = 0;
    if(beatsPerSlot < 1) beatsPerSlot = 1;
    long long slotNumber = (long long)(beatTime / beatsPerSlot);
    double intoSlot = beatTime - (double)slotNumber * beatsPerSlot;
    float threshold = (meshMix(layer * 7919u + (uint32_t)slotNumber) % 1000) / 1000.0f * fadeBeats;
    long long effective = intoSlot >= threshold ? slotNumber : slotNumber - 1;
    return (int)(((effective % slotCount) + slotCount) % slotCount);
}

#endif
