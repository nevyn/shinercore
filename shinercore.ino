#include "M5Unified.h"
#include "FastLED.h"
#include <OverAnimate.h>
#include <SubStrip.h>
#include <ArduinoBLE.h>
#include <Preferences.h>
#include "Util.h"
#include "BeatDetector.h"


enum RunMode
{
    Off = 0,
    On = 1,

    RunModeCount
};
void setMode(RunMode newMode);

#define LAYER_COUNT 10

enum ShinyLayer
{
    CRGB mainColor = CRGB(255, 100, 0);
    CRGB secondaryColor = CRGB(240, 255, 0);
    float speed = 2.0;
    float p_tau = 10.0;
    float p_phi = 4.0;
    int animationIndex = 0;
}

struct ShinySettings
{
    RunMode mode;
    ShinyLayer layers[LAYER_COUNT];
    int currentLayer;
};

////// Main state
ShinySettings localPrefs;
String ownerName = "unknown";
AnimationSystem ansys;
Preferences prefs;
BeatDetector beats;
StripAnimation animations[LAYER_COUNT];



////// Animation things
static const int lstrip_count = 400;
CRGB lstrip[lstrip_count];
SubStrip left(lstrip, lstrip_count);
static const int rstrip_count = 400;
CRGB rstrip[rstrip_count];
SubStrip right(rstrip, rstrip_count);
ProxyStrip allstrips({&left, &right});
CRGB btnled[1];
SubStrip buttonled(btnled, 1);
#include "StripAnimation.h"
#include "Animations.h"


////// Communication things
#include "StoredProperty.h"
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

    FastLED.addLeds<WS2811, GROVE1_PIN, GRB>(lstrip, lstrip_count);
    FastLED.addLeds<WS2811, GROVE2_PIN, GRB>(rstrip, rstrip_count);
    FastLED.addLeds<WS2811, NEO_PIN, GRB>(btnled, 1);
    allstrips.fill(CRGB::Black);
    FastLED.show();

    commsSetup();

    if(M5.getDisplayCount() > 0)
    {
        displaySetup(M5.getDisplay(0));
    }

    beats.setup();
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

    ansys.playElapsedTime(delta);
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

void update(void)
{
    if(M5.BtnA.wasPressed())
    {
        RunMode newMode = (RunMode)(!localPrefs.mode);
        String modeStr = String((int)newMode);
        modeProp.set(modeStr);
    }
}
