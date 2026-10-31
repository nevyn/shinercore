// Host tests for Transition.h: which changes crossfade, and that every ease lands within kCrossfadeSeconds.
#include "FastLED.h"
#include "../ShinyTypes.h"
#include "../Transition.h"
#include <cstdio>
#include <functional>
#include <memory>

static int failures = 0;
static void check(const char *what, bool ok) {
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

// Seconds until step() reports landing, at a fixed frame time; -1 if it never does
static float settleTime(float dt, std::function<bool(float alpha)> step) {
    float alpha = easeAlpha(dt);
    for(int frame = 1; frame * dt < 10; frame++) {
        if(step(alpha)) return frame * dt;
    }
    return -1;
}

// The largest change that still eases must land within the crossfade time, to the frame
static void checkSettles(const char *what, std::function<bool(float alpha)> (*make)()) {
    const float frameTimes[] = {1/200.0f, 1/60.0f, 1/30.0f, 0.1f};
    bool ok = true;
    for(float dt : frameTimes) {
        float t = settleTime(dt, make());
        if(t < 0 || t > kCrossfadeSeconds + dt + 1e-4f) {
            printf("    %s at dt=%.3f: %.3f s\n", what, dt, t);
            ok = false;
        }
    }
    check(what, ok);
}

int main() {
    ShinyLayerSettings base;

    printf("crossfade triggers\n");
    check("identical settings ease", !needsCrossfade(base, base));
    ShinyLayerSettings s = base; s.animationIndex = 3;
    check("animation change crossfades", needsCrossfade(base, s));
    s = base; s.blendMode = BlendModeMultiply;
    check("blend mode change crossfades", needsCrossfade(base, s));
    s = base; s.beatSync = 1;
    check("beat sync toggle eases", !needsCrossfade(base, s));
    s = base; s.mainColor.g += kEaseMaxColorStep;
    check("color step at the threshold eases", !needsCrossfade(base, s));
    s.mainColor.g += 1;
    check("color step past the threshold crossfades", needsCrossfade(base, s));
    s = base; s.secondaryColor.r -= kEaseMaxColorStep + 1;
    check("secondary color counts", needsCrossfade(base, s));
    s = base; s.speed = base.speed * 1.4f;
    check("speed ratio within the threshold eases", !needsCrossfade(base, s));
    s.speed = base.speed / 1.6f;
    check("speed ratio past the threshold crossfades", needsCrossfade(base, s));
    s = base; s.p_tau = 50;
    check("tau 10 -> 50 crossfades", needsCrossfade(base, s));
    s = base; s.p_phi = -base.p_phi;
    check("phi sign flip crossfades", needsCrossfade(base, s));
    s = base; s.p_phi = 0;
    check("phi to zero crossfades", needsCrossfade(base, s));
    s = base; s.opacity = 0;
    check("opacity 1 -> 0 eases", !needsCrossfade(base, s));

    printf("eases land\n");
    checkSettles("color by the threshold, up", []() -> std::function<bool(float)> {
        auto c = std::make_shared<CRGB>(0, 0, 0);
        return [c](float a) { *c = easeColor(*c, CRGB(kEaseMaxColorStep, 0, 0), a); return c->r == kEaseMaxColorStep; };
    });
    checkSettles("color by the threshold, down", []() -> std::function<bool(float)> {
        auto c = std::make_shared<CRGB>(255, 255, 255);
        return [c](float a) { *c = easeColor(*c, CRGB(255, 255 - kEaseMaxColorStep, 255), a); return c->g == 255 - kEaseMaxColorStep; };
    });
    checkSettles("speed by the threshold ratio, faster", []() -> std::function<bool(float)> {
        auto v = std::make_shared<float>(kEaseMaxRatio);
        return [v](float a) { *v = easeSpeed(*v, 1, a); return *v == 1; };
    });
    checkSettles("speed by the threshold ratio, slower", []() -> std::function<bool(float)> {
        auto v = std::make_shared<float>(1 / kEaseMaxRatio);
        return [v](float a) { *v = easeSpeed(*v, 1, a); return *v == 1; };
    });
    checkSettles("tau by the threshold ratio", []() -> std::function<bool(float)> {
        auto v = std::make_shared<float>(-100 / kEaseMaxRatio);
        return [v](float a) { *v = easeParam(*v, -100, a); return *v == -100; };
    });
    checkSettles("full brightness swing", []() -> std::function<bool(float)> {
        auto v = std::make_shared<float>(0);
        return [v](float a) { *v = easeTo(*v, 255, a, 2); return *v == 255; };
    });
    checkSettles("layer gate and opacity, full swing", []() -> std::function<bool(float)> {
        auto v = std::make_shared<float>(1);
        return [v](float a) { *v = easeTo(*v, 0, a, 0.01f); return *v == 0; };
    });

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
