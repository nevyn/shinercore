#include "M5Unified.h"
#include "FastLED.h"
#include <OverAnimate.h>
#include <SubStrip.h>
#include <ArduinoBLE.h>
#include <Preferences.h>
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

LayerAnimation layerAnimations[LAYER_COUNT] = {
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[0]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[1]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[2]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[3]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[4]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[5]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[6]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[7]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[8]),
    LayerAnimation(&backbuffer, &ledstrip, &localPrefs.layers[9]),
};


////// Communication things
#include "StoredProperty.h"
#include "Migration.h"
#include "Comms.h"



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

#define DIRECT_PRESET_PIN_COUNT 4

#if defined(CONFIG_IDF_TARGET_ESP32S3) // M5AtomLiteS3
    #define GROVE1_PIN 1
    #define GROVE2_PIN 2
    #define NEO_PIN 35
    #define HAS_GPIO_CONTROLS
    const int presetPins[DIRECT_PRESET_PIN_COUNT] = {5, 6, 7, 8};
    #define CYCLE_PIN 39
    #define ENABLE_PIN 38
#elif defined(ARDUINO_M5STACK_ATOM) || defined(ARDUINO_M5Stack_ATOM) || defined(ARDUINO_M5STACK_ATOM_ECHO)
    #define GROVE1_PIN 26
    #define GROVE2_PIN 32
    #define NEO_PIN 27
    #define HAS_GPIO_CONTROLS
    const int presetPins[DIRECT_PRESET_PIN_COUNT] = {22, 19, 23, 33};
    #define CYCLE_PIN 21
    #define ENABLE_PIN 25
#elif defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_PLUS) || defined(ARDUINO_M5STACK_STICKC_PLUS)
    #define GROVE1_PIN 32
    #define GROVE2_PIN 33
    #define NEO_PIN 26 // doesn't have one; this pin is just unused
#else
    #error undefined hardware
#endif

void setup(void) {
    M5.begin();
    Serial.begin(115200);

    M5.update();

    FastLED.addLeds<WS2811, GROVE1_PIN, RGB>(rgbs, MAX_LED_COUNT);
    FastLED.addLeds<WS2811, GROVE2_PIN, RGB>(rgbs, MAX_LED_COUNT);
    FastLED.addLeds<WS2811, NEO_PIN, RGB>(btnled, 1);
    ledstrip.fill(CRGB::Black);
    FastLED.show();

    commsSetup();

#ifdef HAS_GPIO_CONTROLS
    for(int i = 0; i < DIRECT_PRESET_PIN_COUNT; i++)
    {
        pinMode(presetPins[i], INPUT_PULLUP);
    }
    pinMode(CYCLE_PIN, INPUT_PULLUP);
    pinMode(ENABLE_PIN, INPUT_PULLUP);
#endif

    if(M5.getDisplayCount() > 0)
    {
        displaySetup(M5.getDisplay(0));
    }

    beats.setup();

    for(int i = 0; i < LAYER_COUNT; i++)
    {
        ansys.addAnimation(&layerAnimations[i]);
    }
}

unsigned long lastMillis;
void loop(void) {
    M5.update();

    unsigned long now = millis();
    if(!lastMillis) {
        lastMillis = now;
    }
    unsigned long diff = now - lastMillis;
    lastMillis = now;
    TimeInterval delta = diff/1000.0;
    
    update();
    commsUpdate(delta);
    applyDerivedState();

    ledstrip.fill(CRGB::Black); // TODO: clear with layer 0 instead, to allow feedback patterns
    ansys.playElapsedTime(delta);
    applyLedColorOrder(rgbs, localPrefs.ledCount);
    FastLED.show();

    if(M5.getDisplayCount() > 0)
    {
        displayUpdate(M5.getDisplay(0));
    }
}

// Settings are canonical; push everything derived from them into the LED and
// animation state every frame, so there are no sync obligations on writes.
void applyDerivedState()
{
    FastLED.setBrightness(localPrefs.mode == Off ? 0 : localPrefs.brightness);
    ledstrip.setNumPixels(localPrefs.ledCount);
    backbuffer.setNumPixels(localPrefs.ledCount);
    buttonled.fill(localPrefs.mode == Off ? CRGB::Black : localPrefs.layers[0].mainColor);
    for(int i = 0; i < LAYER_COUNT; i++)
    {
        layerAnimations[i].duration = localPrefs.layers[i].speed;
    }
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

#ifdef HAS_GPIO_CONTROLS
bool enablePinOverride = false; // true when ENABLE_PIN is actively pulling low
bool cyclePinPressed = false;
unsigned long cyclePinPressedAt = 0;
bool cyclePinLongPressHandled = false;
#endif

void update(void)
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

#ifdef HAS_GPIO_CONTROLS
    // GPIO direct preset buttons: pull low to select that preset
    for(int i = 0; i < DIRECT_PRESET_PIN_COUNT; i++)
    {
        if(digitalRead(presetPins[i]) == LOW && localPrefs.currentPresetIndex != i)
        {
            presetProp.set(i);
        }
    }

    // GPIO cycle button: same behavior as built-in button
    bool cyclePinNow = (digitalRead(CYCLE_PIN) == LOW);
    if(cyclePinNow && !cyclePinPressed)
    {
        cyclePinPressed = true;
        cyclePinPressedAt = millis();
        cyclePinLongPressHandled = false;
    }
    if(cyclePinPressed && !cyclePinLongPressHandled && (millis() - cyclePinPressedAt) > 600)
    {
        cyclePinLongPressHandled = true;
        toggleMode();
    }
    if(!cyclePinNow && cyclePinPressed)
    {
        cyclePinPressed = false;
        if(!cyclePinLongPressHandled) cycleToNextPreset();
    }

    // GPIO enable pin: pull low = force off, floating/high = normal operation
    bool enablePulledLow = (digitalRead(ENABLE_PIN) == LOW);
    if(enablePulledLow && !enablePinOverride)
    {
        enablePinOverride = true;
        modeProp.set(Off);
    }
    else if(!enablePulledLow && enablePinOverride)
    {
        enablePinOverride = false;
        modeProp.set(On);
    }
#endif
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
