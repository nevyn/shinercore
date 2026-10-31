#ifndef __MESH_LOGIC__H
#define __MESH_LOGIC__H
#include <string.h>
#include <stdint.h>
#include "ShinyTypes.h"
#include "Codecs.h"

// The pure half of the mesh: wire formats and decision math, no radio.
// Host-tested in test/meshtest.cpp; Mesh.h owns everything with side effects.

#define kMeshVersion 4
#define kMeshFlagHasMic 1
#define kMeshFlagConfident 2
#define kMeshFlagFollowing 4 // this grid is itself relayed from a leader

enum MeshFrameType : uint8_t
{
    MeshFrameBeat = 1,
    MeshFramePreset = 2,
};

// Group gating: you only mesh with your own camp. The group name is normalized
// (trimmed, ASCII-lowercased, so two people typing the camp name agree) and
// FNV-1a hashed into every frame header; receivers drop foreign hashes. Not
// security, just fencing - and the empty string is a group like any other.
static inline uint32_t meshGroupHash(const char *group)
{
    const char *s = group;
    const char *e = group + strlen(group);
    while(s < e && (*s == ' ' || *s == '\t')) s++;
    while(e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
    uint32_t h = 2166136261u;
    for(; s < e; s++)
    {
        char c = *s;
        if(c >= 'A' && c <= 'Z') c += 32;
        h = (h ^ (uint8_t)c) * 16777619u;
    }
    return h;
}

// Every frame starts with this; the group check happens before the type switch
struct __attribute__((packed)) MeshFrameHeader
{
    uint8_t version;
    uint8_t type;
    uint32_t groupHash;
};

struct __attribute__((packed)) MeshBeatFrame
{
    uint8_t version;
    uint8_t type;
    uint32_t groupHash;
    uint8_t flags;      // kMeshFlag*
    float period;       // seconds per beat
    float phase;        // 0..1 within the current beat, at send time
    double beatTime;    // fractional beats since sender's boot
    float confidence;   // 0..1
    uint8_t color[3];   // sender's layer 0 mainColor
    uint8_t leaderMac[6]; // whom the sender follows; zeros when leading.
                          // Lets a core refuse to take the clock from its own
                          // follower - the following FLAG alone is a beacon
                          // interval stale, which let two booting cores follow
                          // each other and oscillate.
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
    uint8_t opacity;    // 0-255 for 0-1
};

struct __attribute__((packed)) MeshPresetFrame
{
    uint8_t version;
    uint8_t type;
    uint32_t groupHash;
    uint8_t layerCount;
    MeshPresetLayer layers[LAYER_COUNT]; // only layerCount are sent
};

static inline void meshEncodeLayer(int layer, const ShinyLayerSettings &l, MeshPresetLayer &out)
{
    out.layer = layer;
    out.animation = l.animationIndex;
    out.blendMode = l.blendMode;
    out.beatSync = l.beatSync;
    out.color1[0] = l.mainColor.r; out.color1[1] = l.mainColor.g; out.color1[2] = l.mainColor.b;
    out.color2[0] = l.secondaryColor.r; out.color2[1] = l.secondaryColor.g; out.color2[2] = l.secondaryColor.b;
    out.speed = l.speed;
    out.tau = l.p_tau;
    out.phi = l.p_phi;
    out.opacity = (uint8_t)roundf(clampTo(l.opacity, 0.0f, 1.0f) * 255);
}

// Decodes with hostile-input clamps: anyone can broadcast ESP-NOW frames. The caller
// checks in.layer.
static inline void meshDecodeLayer(const MeshPresetLayer &in, ShinyLayerSettings &l)
{
    l.animationIndex = in.animation < (int)animationNames.size() ? in.animation : 0;
    l.blendMode = in.blendMode < BlendModeCount ? (LayerBlendMode)in.blendMode : BlendModeAdd;
    l.beatSync = in.beatSync ? 1 : 0;
    l.mainColor = CRGB(in.color1[0], in.color1[1], in.color1[2]);
    l.secondaryColor = CRGB(in.color2[0], in.color2[1], in.color2[2]);
    l.speed = isfinite(in.speed) ? clampTo(in.speed, 0.001f, 100.0f) : 1.0f;
    l.p_tau = isfinite(in.tau) ? clampTo(in.tau, -100.0f, 100.0f) : 10.0f;
    l.p_phi = isfinite(in.phi) ? clampTo(in.phi, -100.0f, 100.0f) : 4.0f;
    l.opacity = in.opacity / 255.0f;
}

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
    if(fadeBeats > beatsPerSlot) fadeBeats = beatsPerSlot; // every layer must cross within its slot
    long long slotNumber = (long long)(beatTime / beatsPerSlot);
    double intoSlot = beatTime - (double)slotNumber * beatsPerSlot;
    float threshold = (meshMix(layer * 7919u + (uint32_t)slotNumber) % 1000) / 1000.0f * fadeBeats;
    long long effective = intoSlot >= threshold ? slotNumber : slotNumber - 1;
    return (int)(((effective % slotCount) + slotCount) % slotCount);
}

#endif
