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
int StoredMultiProperty::currentLayer = 1;
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

#if defined(CONFIG_IDF_TARGET_ESP32S3) // M5AtomLiteS3
    #define GROVE1_PIN 1
    #define GROVE2_PIN 2
    #define NEO_PIN 35
#elif defined(ARDUINO_M5STACK_ATOM) || defined(ARDUINO_M5Stack_ATOM) || defined(ARDUINO_M5STACK_ATOM_ECHO)
    #define GROVE1_PIN 26
    #define GROVE2_PIN 32
    #define NEO_PIN 27
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
    if(M5.BtnA.isHolding()) {
        logger.println("CLEARING SETTINGS DUE TO BUTTON HELD\n");
        prefs.clear();   
    }

    FastLED.addLeds<WS2811, GROVE1_PIN, RGB>(rgbs, MAX_LED_COUNT);
    FastLED.addLeds<WS2811, GROVE2_PIN, RGB>(rgbs, MAX_LED_COUNT);
    FastLED.addLeds<WS2811, NEO_PIN, RGB>(btnled, 1);
    ledstrip.fill(CRGB::Black);
    FastLED.show();

    commsSetup();

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

    ledstrip.fill(CRGB::Black); // TODO: clear with layer 0 instead, to allow feedback patterns
    ansys.playElapsedTime(delta);
    applyLedColorOrder(rgbs, localPrefs.ledCount);
    FastLED.show();

    if(M5.getDisplayCount() > 0)
    {
        displayUpdate(M5.getDisplay(0));
    }
}

void setMode(RunMode newMode)
{
    localPrefs.mode = newMode;
    buttonled.fill(localPrefs.mode==Off ? CRGB::Black :  localPrefs.layers[0].mainColor);

    if(localPrefs.mode == Off) {
        FastLED.setBrightness(0);
    } else {
        FastLED.setBrightness(brightnessProp.get().toInt());
    }
}

void setLayer(int newLayer)
{
    localPrefs.currentLayerIndex = newLayer;
    StoredMultiProperty::useLayer(newLayer);

    // re-publish values at this layer to bluetooth so app sees them, hopefully
    for(const auto& prop: layerProps)
    {
        prop->writeToChara();
    }
}

void update(void)
{
    if(M5.BtnA.wasPressed())
    {
        RunMode newMode = (RunMode)(!localPrefs.mode);
        String modeStr = String((int)newMode);
        modeProp.set(modeStr);
    }
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
