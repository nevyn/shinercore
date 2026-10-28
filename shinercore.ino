#include "M5Unified.h"
#include "FastLED.h"
#include <OverAnimate.h>
#include <SubStrip.h>
#include <ArduinoBLE.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include "Util.h"
#include "BeatDetector.h"
#include "LayerAnimation.h"
#include "Animations.h"
#include "ShinyTypes.h"
#include "Transition.h"

////// Main state
ShinySettings localPrefs;
String ownerName = "unknown";
AnimationSystem ansys;
Preferences prefs;
BeatDetector beats;



////// Animation things
CRGB rgbs[MAX_LED_COUNT];
SubStrip ledstrip(rgbs, MAX_LED_COUNT);

CRGB bbstrip[MAX_LED_COUNT];
SubStrip backbuffer(bbstrip, MAX_LED_COUNT);
CRGB btnled[1];
SubStrip buttonled(btnled, 1);

// Animations render from these, which trail the canonical settings (below in
// applyDerivedState) so parameter changes glide instead of jumping
ShinyLayerSettings renderedLayers[LAYER_COUNT];
float renderedBrightness = 0;
// The frame that a crossfade dissolves away from (docs/transitions.md)
CRGB fadeFrom[MAX_LED_COUNT];
float crossfadeLeft = 0;

LayerAnimation layerAnimations[LAYER_COUNT] = {
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[0]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[1]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[2]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[3]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[4]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[5]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[6]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[7]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[8]),
    LayerAnimation(&backbuffer, &ledstrip, &renderedLayers[9]),
};


////// Communication things
#include "StoredProperty.h"
#include "Migration.h"
#include "Comms.h"
#include "Mesh.h"
Mesh mesh;
Mesh *Mesh::_instance = nullptr;
void meshGroupChanged() { mesh.groupChanged(); }
#include "WiringRuntime.h"



///// Display things
void displaySetup(M5GFX &display)
{
    display.setTextScroll(true);
}

void displayUpdate(M5GFX &display)
{
    //display.clear(TFT_BLACK);
    //display.drawString("Hello world", 0, 0);
}


///// Runtime things

void setup(void) {
    M5.begin();
    Serial.begin(115200);

    M5.update();

    commsSetup();
    wiringSetup(); // after commsSetup has loaded the wiring

    // The builtin pixel is GRB hardware on every variant, and unlike the strip
    // buffer it never passes through applyLedColorOrder.
    // Registered after the strips, or FastLED.show() can hang (docs/wiring.md, LED pins).
    FastLED.addLeds<WS2811, NEO_PIN, GRB>(btnled, 1);

    if(M5.getDisplayCount() > 0)
    {
        displaySetup(M5.getDisplay(0));
    }

    for(int i = 0; i < LAYER_COUNT; i++)
    {
        ansys.addAnimation(&layerAnimations[i]);
    }

    // A hang anywhere (a wedged radio stack, a stuck driver) reboots into
    // working lights instead of a dead jacket; settings reload from NVS.
    // After commsSetup, so its deliberate can't-even-boot halts stay halts.
    esp_task_wdt_init(8, true);
    esp_task_wdt_add(NULL);
}

unsigned long lastMillis;
void loop(void) {
    esp_task_wdt_reset();
    M5.update();

    unsigned long now = millis();
    if(!lastMillis) {
        lastMillis = now;
    }
    unsigned long diff = now - lastMillis;
    lastMillis = now;
    TimeInterval delta = diff/1000.0;
    if(delta > 0.1) delta = 0.1; // a stalled frame plays out in slow motion, not as a teleport

    update(delta);
    commsUpdate(delta);
    beats.update(delta);
    mesh.update(delta); // may pull the beat grid; before deriving render state
    applyDerivedState(delta);

    ledstrip.fill(CRGB::Black); // TODO: clear with layer 0 instead, to allow feedback patterns
    ansys.playElapsedTime(delta);
    applyCrossfade(delta);
    applyLedColorOrder(rgbs, localPrefs.ledCount);
    if(localPrefs.maxCurrentMa && stripOutputs)
    {
        // Every output mirrors this buffer, so each gets an equal share of the
        // budget. Not FastLED.setMaxPowerInVoltsAndMilliamps: that sums the
        // controllers, registered at MAX_LED_COUNT, and the phantom LEDs'
        // ~1mA/LED dark power eats most of the budget.
        FastLED.setBrightness(calculate_max_brightness_for_power_vmA(
            rgbs, localPrefs.ledCount, FastLED.getBrightness(), 5, localPrefs.maxCurrentMa / stripOutputs));
    }
    FastLED.show();

    if(M5.getDisplayCount() > 0)
    {
        displayUpdate(M5.getDisplay(0));
    }
}

static const float kBeatChaseGain = 2.0f;   // rate boost per cycle of beat-phase error

// Dissolves from the frame on the strip now; called before this frame renders.
void startCrossfade()
{
    memcpy(fadeFrom, rgbs, sizeof(rgbs));
    applyLedColorOrder(fadeFrom, localPrefs.ledCount); // undoes the last frame's swap: every order is its own inverse
    crossfadeLeft = kCrossfadeSeconds;
}

void applyCrossfade(TimeInterval dt)
{
    if(crossfadeLeft <= 0) return;
    crossfadeLeft -= dt;
    float progress = crossfadeLeft <= 0 ? 1 : 1 - crossfadeLeft / kCrossfadeSeconds;
    uint8_t amount = progress * 255 + 0.5f;
    for(int i = 0; i < localPrefs.ledCount; i++)
    {
        rgbs[i] = blend(fadeFrom[i], rgbs[i], amount);
    }
}

// Settings are canonical; push everything derived from them into the LED and
// animation state every frame, so there are no sync obligations on writes.
// Small changes ease; large ones, and any change of animation or blend mode,
// snap the rendered settings and crossfade the pixels (docs/transitions.md).
void applyDerivedState(TimeInterval dt)
{
    float alpha = easeAlpha(dt);

    renderedBrightness = easeTo(renderedBrightness, localPrefs.mode == Off ? 0 : localPrefs.brightness, alpha, 2);
    FastLED.setBrightness((uint8_t)roundf(renderedBrightness));
    ledstrip.setNumPixels(localPrefs.ledCount);
    backbuffer.setNumPixels(localPrefs.ledCount);
    buttonled.fill(localPrefs.mode == Off ? CRGB::Black : renderedLayers[0].mainColor);

    // own settings, or whichever preset the mesh carousel has on stage
    const ShinyLayerSettings *targets[LAYER_COUNT];
    bool crossfade = false;
    for(int i = 0; i < LAYER_COUNT; i++)
    {
        targets[i] = mesh.carouselLayers(i);
        crossfade = crossfade || needsCrossfade(renderedLayers[i], *targets[i]);
    }
    if(crossfade) startCrossfade();

    for(int i = 0; i < LAYER_COUNT; i++)
    {
        const ShinyLayerSettings &target = *targets[i];
        ShinyLayerSettings &shown = renderedLayers[i];
        if(crossfade)
        {
            shown = target;
        }
        else
        {
            shown.mainColor = easeColor(shown.mainColor, target.mainColor, alpha);
            shown.secondaryColor = easeColor(shown.secondaryColor, target.secondaryColor, alpha);
            shown.speed = easeSpeed(shown.speed, target.speed, alpha);
            shown.p_tau = easeParam(shown.p_tau, target.p_tau, alpha);
            shown.p_phi = easeParam(shown.p_phi, target.p_phi, alpha);
            shown.beatSync = target.beatSync;
        }

        // Lands exactly on 0, so a closed gate stops the layer rendering
        layerAnimations[i].gain = easeTo(layerAnimations[i].gain, layerGate[i] ? 1 : 0, alpha, 0.01f);

        if(shown.beatSync)
        {
            // speed is beats per cycle: phase-lock the animation to the beat
            // grid by modulating its playback rate toward where the cycle
            // should be, wrapped to the nearest cycle so tempo corrections and
            // toggling sync mid-flight glide over at most half a cycle.
            double targetCycles = beats.beatTime() / shown.speed;
            double err = remainder(targetCycles - layerAnimations[i].time, 1.0);
            float chase = 1.0f + (float)err * kBeatChaseGain;
            chase = chase < 0.5f ? 0.5f : chase > 2.0f ? 2.0f : chase;
            layerAnimations[i].duration = beats.period() * shown.speed / chase;
        }
        else
        {
            layerAnimations[i].duration = shown.speed; // seconds per cycle
        }
    }

    if(localPrefs.micEnabled && !beats.running()) beats.begin();
    else if(!localPrefs.micEnabled && beats.running()) beats.end();
    if(localPrefs.meshEnabled && !mesh.running()) mesh.begin();
    else if(!localPrefs.meshEnabled && mesh.running()) mesh.end();
}

bool presetHasAnimations(int presetIndex)
{
    for(int layer = 0; layer < LAYER_COUNT; layer++)
    {
        String val = prefs.getString(layerKey("animation", layer, presetIndex).c_str(), "Nothing");
        if(val != "Nothing") return true;
    }
    return false;
}

void cycleToNextPreset()
{
    int nextPreset = localPrefs.currentPresetIndex;
    for(int i = 0; i < PRESET_COUNT - 1; i++)
    {
        nextPreset = (nextPreset + 1) % PRESET_COUNT;
        if(presetHasAnimations(nextPreset)) break;
    }
    presetProp.set(nextPreset);
}

void toggleMode()
{
    modeProp.set(localPrefs.mode == Off ? On : Off);
}

void update(TimeInterval dt)
{
    wiringUpdate(dt);
}

// Apply color order transformation before FastLED.show()
// FastLED is configured with RGB order, so we swap colors to match the actual strip
void applyLedColorOrder(CRGB* strip, int count)
{
    switch(localPrefs.ledColorOrder) {
        case LedOrderRGB:
            // No transformation needed - matches FastLED template
            break;
        case LedOrderGRB:
            // Swap R and G
            for(int i = 0; i < count; i++) {
                std::swap(strip[i].r, strip[i].g);
            }
            break;
        case LedOrderBGR:
            // Swap R and B
            for(int i = 0; i < count; i++) {
                std::swap(strip[i].r, strip[i].b);
            }
            break;
        default:
            break;
    }
}
