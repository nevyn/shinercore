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

    // The builtin pixel is GRB hardware on every variant, and unlike the strip
    // buffer it never passes through applyLedColorOrder
    FastLED.addLeds<WS2811, NEO_PIN, GRB>(btnled, 1);
    ledstrip.fill(CRGB::Black);
    FastLED.show();

    commsSetup();
    wiringSetup(); // after commsSetup has loaded the wiring

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

// Exponential approach: how far to move toward a target this frame
static const float kSettingsSlewTime = 0.2; // seconds to cover ~63% of a change
static const float kBeatChaseGain = 2.0f;   // rate boost per cycle of beat-phase error
static float slew(float current, float target, float alpha)
{
    return current + (target - current) * alpha;
}
static CRGB slewColor(const CRGB &current, const CRGB &target, float alpha)
{
    // round away from current so the last step actually lands on the target
    CRGB out;
    for(int ch = 0; ch < 3; ch++)
    {
        float v = slew(current.raw[ch], target.raw[ch], alpha);
        out.raw[ch] = target.raw[ch] > current.raw[ch] ? (uint8_t)ceilf(v) : (uint8_t)floorf(v);
    }
    return out;
}

// Settings are canonical; push everything derived from them into the LED and
// animation state every frame, so there are no sync obligations on writes.
// Continuous parameters slew toward their targets so changes glide: colors
// crossfade, wave periods stretch, brightness fades, presets morph into each
// other. Discrete parameters (animation, blend mode) snap.
void applyDerivedState(TimeInterval dt)
{
    float alpha = 1 - expf(-dt / kSettingsSlewTime);

    renderedBrightness = slew(renderedBrightness, localPrefs.mode == Off ? 0 : localPrefs.brightness, alpha);
    FastLED.setBrightness((uint8_t)roundf(renderedBrightness));
    ledstrip.setNumPixels(localPrefs.ledCount);
    backbuffer.setNumPixels(localPrefs.ledCount);
    buttonled.fill(localPrefs.mode == Off ? CRGB::Black : renderedLayers[0].mainColor);

    for(int i = 0; i < LAYER_COUNT; i++)
    {
        // own settings, or whichever preset the mesh carousel has on stage
        const ShinyLayerSettings &target = *mesh.carouselLayers(i);
        ShinyLayerSettings &shown = renderedLayers[i];
        shown.mainColor = slewColor(shown.mainColor, target.mainColor, alpha);
        shown.secondaryColor = slewColor(shown.secondaryColor, target.secondaryColor, alpha);
        shown.speed = slew(shown.speed, target.speed, alpha);
        shown.p_tau = slew(shown.p_tau, target.p_tau, alpha);
        shown.p_phi = slew(shown.p_phi, target.p_phi, alpha);
        shown.blendMode = target.blendMode;
        shown.animationIndex = target.animationIndex;
        shown.beatSync = target.beatSync;

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

bool builtinLongPressHandled = false;

void update(TimeInterval dt)
{
    // Built-in button: short press = cycle preset, long press = on/off
    if(M5.BtnA.wasPressed())
    {
        builtinLongPressHandled = false;
    }
    if(!builtinLongPressHandled && M5.BtnA.pressedFor(600))
    {
        builtinLongPressHandled = true;
        toggleMode();
    }
    if(M5.BtnA.wasReleased() && !builtinLongPressHandled)
    {
        cycleToNextPreset();
    }

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
