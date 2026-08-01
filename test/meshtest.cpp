// Host tests for MeshLogic.h: frame layouts, leader ranking, carousel slots.
#include "FastLED.h"
#include "../ShinyTypes.h"
#include "../MeshLogic.h"
#include <cstdio>

static int failures = 0;
static void check(const char *what, bool ok) {
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

static const uint8_t macA[6] = {0x11,0,0,0,0,1};
static const uint8_t macB[6] = {0x22,0,0,0,0,2};

int main() {
    printf("frame layouts\n");
    check("beat frame fits", sizeof(MeshBeatFrame) <= 250);
    check("beat frame is packed", sizeof(MeshBeatFrame) == 3 + 4 + 4 + 8 + 4 + 3 + 6);
    check("full preset frame fits ESP-NOW", sizeof(MeshPresetFrame) <= 250);
    check("preset layer is packed", sizeof(MeshPresetLayer) == 4 + 3 + 3 + 12);

    printf("ranking\n");
    check("confidence dominates", meshOutranks({0.9f,false,false,macB}, {0.5f,false,true,macA}));
    check("hysteresis: small conf edge doesn't outrank", !meshOutranks({0.55f,false,false,macB}, {0.5f,false,false,macA}));
    check("own grid beats relay in band", meshOutranks({0.5f,false,false,macB}, {0.55f,true,false,macA}));
    check("relay loses in band even with mic", meshOutranks({0.5f,false,false,macB}, {0.55f,true,true,macA}));
    check("mic breaks tie", meshOutranks({0.5f,false,true,macB}, {0.5f,false,false,macA}));
    check("low mac breaks tie", meshOutranks({0.5f,false,false,macA}, {0.5f,false,false,macB}));

    // The A<->B follow cycle: A follows B (conf 0.9*B), B must not follow A back.
    MeshRank aFollowing = {0.81f, true, false, macA};   // A relaying B's 0.9 grid
    MeshRank bLeader = {0.9f, false, true, macB};
    check("no follow cycle", !meshOutranks(aFollowing, bLeader));
    // A follower's SELF-view must also lose ties to its leader (0.9x conf is
    // always within the hysteresis band), or it defects and re-follows every
    // beacon: rankSelf must report the real following flag for this to hold.
    MeshRank followerSelf = {0.63f, true, true, macA};  // low mac, has mic, relaying
    MeshRank itsLeader = {0.7f, false, true, macB};
    check("follower self-view stays loyal in band", !meshOutranks(followerSelf, itsLeader));
    check("follower still defects when clearly better", meshOutranks({0.85f, true, true, macA}, itsLeader));
    // asymmetry holds everywhere it matters
    bool sym_ok = true;
    float confs[] = {0.0f, 0.45f, 0.5f, 0.55f, 1.0f};
    for(float ca : confs) for(float cb : confs)
        for(int fa = 0; fa < 2; fa++) for(int fb = 0; fb < 2; fb++)
            for(int ma = 0; ma < 2; ma++) for(int mb = 0; mb < 2; mb++) {
                MeshRank a = {ca, (bool)fa, (bool)ma, macA}, b = {cb, (bool)fb, (bool)mb, macB};
                if(meshOutranks(a, b) && meshOutranks(b, a)) sym_ok = false;
            }
    check("outranks is asymmetric over grid of cases", sym_ok);

    printf("carousel slots\n");
    check("single slot pins to 0", meshCarouselSlot(123.4, 8, 1, 3, 2.0f) == 0);
    check("negative time clamps", meshCarouselSlot(-0.3, 8, 4, 0, 2.0f) == 0 || meshCarouselSlot(-0.3, 8, 4, 0, 2.0f) == 3);
    // well past the fade window, every layer agrees on the slot
    bool agree = true;
    for(int layer = 0; layer < LAYER_COUNT; layer++)
        if(meshCarouselSlot(8 * 5 + 3.0, 8, 4, layer, 2.0f) != (5 % 4)) agree = false;
    check("all layers agree mid-slot", agree);
    // during the fade window, layers are split between old and new slot
    int oldCount = 0, newCount = 0;
    for(int layer = 0; layer < LAYER_COUNT; layer++) {
        int s = meshCarouselSlot(8 * 5 + 0.9, 8, 4, layer, 2.0f);
        if(s == 5 % 4) newCount++;
        else if(s == 4 % 4) oldCount++;
    }
    check("fade window straddles slots", oldCount > 0 && newCount > 0 && oldCount + newCount == LAYER_COUNT);
    // slots advance and wrap
    check("slot advances", meshCarouselSlot(8 * 6 + 3.0, 8, 4, 0, 2.0f) == 6 % 4);
    check("degenerate beatsPerSlot survives", meshCarouselSlot(10.0, 0, 4, 0, 2.0f) >= 0);
    // fade window longer than the slot: every layer must still cross over
    bool oneBeansAgree = true;
    for(int layer = 0; layer < LAYER_COUNT; layer++)
        if(meshCarouselSlot(1 * 37 + 0.999, 1, 4, layer, 2.0f) != 37 % 4) oneBeansAgree = false;
    check("carouselBeats=1 still reaches every layer", oneBeansAgree);

    printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
