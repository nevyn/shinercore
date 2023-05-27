#include "M5Unified.h"
#include "FastLED.h"
#include <OverAnimate.h>
#include <SubStrip.h>
#include <ArduinoBLE.h>
#include <Preferences.h>

class Logger : public Print
{
    virtual size_t write(uint8_t c)
    {
        Serial.write(c);
        if(M5.getDisplayCount() > 0)
        {
            M5GFX &display = M5.getDisplay(0);
            return display.write(c);
        }
    }
};
Logger logger;

////// Animation things

enum RunMode
{
    Off = 0,
    DoubleCrawl = 1,

    RunModeCount
};

static const int lstrip_count = 400;
CRGB lstrip[lstrip_count];
SubStrip left(lstrip, lstrip_count);
static const int rstrip_count = 400;
CRGB rstrip[rstrip_count];
SubStrip right(rstrip, rstrip_count);
ProxyStrip allstrips({&left, &right});

CRGB btnled[1];
SubStrip buttonled(btnled, 1);

struct ShinySettings {
    CRGB mainColor(255, 100, 0);
    CRGB secondaryColor(240, 255, 0);
    RunMode mode;
    float speed = 2.0;
    float p_tau = 10.0;
    float p_phi = 4.0;
};

ShinySettings localPrefs;

String ownerName = "unknown";

AnimationSystem ansys;

typedef void(*StripFunc)(Animation*, SubStrip *strip, ShinySettings *prefs, float);
class StripAnimation : public Animation
{
public:
    StripFunc func;
    SubStrip *strip;
    ShinySettings *prefs;
    StripAnimation(StripFunc func, SubStrip *strip, TimeInterval duration = 1.0, bool repeats = false) 
      : Animation(duration, repeats), func(func), strip(strip) {}
protected:
    void animate(float absoluteTime) { func(this, strip, prefs, absoluteTime); }
};

float frand(void)
{
  return random(1000)/1000.0;
}

// Over the input range of 0.0f->1.0f, returns a single sinusoidal cycle
// starting at 0.0f, with the peak at 1.0f.
float curve(float progress)
{
    return sin((progress-0.25)*6.28f)/2.0f + 0.5f;
}

// Animation that crawls two sine waves against each other
void DoubleCrawlAnim(Animation *self, SubStrip *strip, ShinySettings *prefs, float t)
{
    for(int i = 0; i < strip->numPixels(); i++)
    {
        strip->set(i, prefs->mainColor * (gammaf(curve(t - i/prefs->p_tau))/2.0f) + prefs->secondaryColor * (gammaf(curve(t + i/prefs->p_phi))/2.0f));
    }
}
StripAnimation doubleCrawlAnim(DoubleCrawlAnim, &allstrips, 2.0, true);

// simple fade between two colors
void BreatheAnim(Animation *self, SubStrip *strip, ShinySettings *prefs, float t)
{
    for(int i = 0; i < strip->numPixels(); i++)
    {
        strip->set(i, prefs->mainColor * gammaf(curve(t)) + prefs->secondaryColor * gammaf(curve(t+0.5)));
    }
}
StripAnimation breatheAnim(BreatheAnim, &allstrips, 2.0, true);

// Port of Fire2012 by Mark Kriegsman
// Adapted from https://github.com/bportaluri/ALA/blob/master/src/AlaLedRgb.cpp#L649 :) 
void FireAnim(Animation *self, SubStrip *strip, ShinySettings *prefs, float t)
{
    // does not work well with 400...
    static const int numPixels = 100;

    // COOLING: How much does the air cool as it rises?
    // Less cooling = taller flames.  More cooling = shorter flames.
    // Default 550
    #define COOLING  600

    // SPARKING: What chance (out of 255) is there that a new spark will be lit?
    // Higher chance = more roaring fire.  Lower chance = more flickery fire.
    // Default 120, suggested range 50-200.
    #define SPARKING 120

    // Array of temperature readings at each simulation cell
    static byte heat[numPixels];

    // Step 1.  Cool down every cell a little
    int rMax = (COOLING / numPixels) + 2;
    for(int i=0; i<numPixels; i++)
    {
        heat[i] = std::max(((int)heat[i]) - (int)random(0, rMax), 0);
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for(int k=numPixels-1; k>=3; k--)
    {
        heat[k] = ((int)heat[k - 1] + (int)heat[k - 2] + (int)heat[k - 3] ) / 3;
    }

    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if(random(255) < SPARKING)
    {
        int y = random(7);
        heat[y] = std::min(heat[y] + (int)random(160, 255), 255);
    }

    // Step 4.  Map from heat cells to LED colors
    for(int j=0; j<numPixels; j++)
    {
        CRGB color = prefs->mainColor.lerp8(prefs->secondaryColor, heat[j]);
        strip->set(j, color);
    }
}
StripAnimation fireAnim(FireAnim, &allstrips, 2.0, true);


std::array<StripAnimation*, 3> animations = {&doubleCrawlAnim, &breatheAnim, &fireAnim};

////// Communication things

#define kDescriptorUserDesc "2901"
#define kDescriptorClientCharacteristic "2902"
#define kDescriptorServerCharacteristic "2903"
#define kDescriptorPresentationFormat "2904"
#define kDescriptorPresentationFormat_S32 "10"
#define kDescriptorPresentationFormat_Float32 "14"
#define kDescriptorPresentationFormat_String "19"
#define kDescriptorValidRange "2906"

Preferences prefs;
class StoredProperty 
{
public:
    StoredProperty(const char *uuid, const char *key, String defaultValue, const char *range, std::function<void(const String&)> applicator)
      : chara(uuid, BLERead | BLEWrite, 36),
        key(key),
        value(defaultValue),
        defaultValue(defaultValue),
        applicator(applicator),
        nameDescriptor(kDescriptorUserDesc, key),
        formatDescriptor(kDescriptorPresentationFormat, kDescriptorPresentationFormat_String),
        rangeDescriptor(kDescriptorValidRange, range)
    {
        chara.addDescriptor(nameDescriptor);
        chara.addDescriptor(formatDescriptor);
    }
    void set(const String &newVal)
    {
        value = newVal;
        save();
        chara.writeValue(value);
        applicator(value);
    }
    String get()
    {
        return value;
    }

    const char* uuid()
    {
        return chara.uuid();
    }

    void advertise(BLEService &onService)
    {
        onService.addCharacteristic(chara);
    }
    void poll()
    {
        if(chara.written())
        {
            String oldValue = value;
            String newValue = chara.value();
            set(newValue);
        }
    }
    void load()
    {
        value = prefs.getString(key, value);
        chara.writeValue(value);
        applicator(value);
        logger.print(key); logger.print(" := "); logger.println(value);
    }
protected:
    void save()
    {
        if(value.isEmpty() || value.equals(" ") || value.equals(defaultValue))
        {
            prefs.remove(key);
            value = defaultValue;
        }
        else if(prefs.putString(key, value) == 0)
        {
            logger.println("failed to store preferences!");
            while (1);
        }
        logger.print(key); logger.print(" = "); logger.println(value);
    }
protected:
    BLEStringCharacteristic chara;
    const char *key;
    String value;
    const String defaultValue;
    std::function<void(const String&)> applicator;

    BLEDescriptor nameDescriptor;
    BLEDescriptor formatDescriptor;
    BLEDescriptor rangeDescriptor;
};

CRGB rgbFromString(const String &str)
{
    int firstSpace = str.indexOf(' ');
    int secondSpace = str.lastIndexOf(' ');
    return CRGB(
        str.substring(0, firstSpace).toInt(), 
        str.substring(firstSpace+1, secondSpace).toInt(),
        str.substring(secondSpace+1, str.length()).toInt()
    );
}

BLEService shinerService("6c0de004-629d-4717-bed5-847fddfbdc2e");

StoredProperty speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", "0.5", "0.0,100.0", [](const String &newValue) {
    for(const auto& anim: animations)
    {
        anim->duration = newValue.toFloat();
    }
});
StoredProperty colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color1", "255 100 0", "0 0 0,255 255 255", [](const String &newValue) {
    localPrefs.mainColor = rgbFromString(newValue);
    if (localPrefs.mode == 1) buttonled.fill(localPrefs.mainColor);
});
StoredProperty color2Prop("83595a76-1b17-4158-bcee-e702c3165caf", "color2", "240 255 0", "0 0 0,255 255 255", [](const String &newValue) {
    localPrefs.secondaryColor = rgbFromString(newValue);
});
StoredProperty modeProp("70d4cabe-82cc-470a-a572-95c23f1316ff", "mode", "1", "0,1", [](const String &newValue) {
    setMode((RunMode)newValue.toInt());
});
StoredProperty brightnessProp("2B01", "brightness", "255", "0-255", [](const String &newValue) {
    FastLED.setBrightness(newValue.toInt());
});
StoredProperty tauProp("d879c81a-09f0-4a24-a66c-cebf358bb97a", "tau", "10.0", "-100.0,100.0", [](const String &newValue) {
    localPrefs.p_tau = newValue.toFloat();
});
StoredProperty phiProp("df6f0905-09bd-4bf6-b6f5-45b5a4d20d52", "phi", "4.0", "-100.0,100.0", [](const String &newValue) {
    localPrefs.p_phi = newValue.toFloat();
});
StoredProperty nameProp("7ad50f2a-01b5-4522-9792-d3fd4af5942f", "name", "unknown", "", [](const String &newValue) {
    ownerName = newValue;
});

std::array<StoredProperty*, 8> props = {&speedProp, &colorProp, &color2Prop, &modeProp, &brightnessProp, &tauProp, &phiProp, &nameProp};

std::vector<BLEDevice> connectedCores;
std::vector<BLEDevice> failedCores;

class RemoteCore
{
public:
    BLEDevice device;
    BLECharacteristic colorProp;
    bool connected;
    bool failed;
};
std::vector<RemoteCore*> remoteCores;

void commsSetup(void)
{
    if (!prefs.begin("shinercore"))
    {
        logger.println("failed to read preferences!");
        while (1);
    }

    if (!BLE.begin()) {
        logger.println("starting Bluetooth® Low Energy module failed!");
        while (1);
    }

    for(const auto& prop: props)
    {
        prop->load();
        prop->advertise(shinerService);
    }

    String name = ownerName + "'s shinercore";
    BLE.setDeviceName(name.c_str());
    BLE.setLocalName(name.c_str());
    
    BLE.setAdvertisedService(shinerService);
    BLE.addService(shinerService);
    if (BLE.advertise()) {
        logger.println("Advertising...");
    }

    // scan for other shinercores
    BLE.scanForUuid(shinerService.uuid());
}

void commsUpdate(void)
{
    BLE.poll();

    for(const auto& prop: props)
    {
        prop->poll();
    }

    // TODO: convert this to not-spaghetti and use RemoteCore to properly interrogate and track state
    BLEDevice otherCore = BLE.available();
    if(otherCore && std::find(failedCores.begin(), failedCores.end(), otherCore) == failedCores.end())
    {
        // can't connect while scanning
        BLE.stopScan();

        logger.printf("Connecting to %s...\n", otherCore.localName().c_str());
        if(otherCore.connect())
        {
            logger.printf("Connected!\n");
            connectedCores.push_back(otherCore);

            BLECharacteristic mainColor = otherCore.characteristic(colorProp.uuid());
            if(mainColor)
            {
                char colorStr[255];
                // see also: characteristic.valueUpdated()
                mainColor.readValue(colorStr, 255);
                logger.printf("That core has primary color %s\n", colorStr);
            } else {
                logger.printf("Booo, can't read its color prop :(\n");
            }

        } else {
            logger.printf("Failed to connect :'(\n");
            failedCores.push_back(otherCore);
        }

        // all done connecting, keep scanning
        BLE.scanForUuid(shinerService.uuid());
    }

}

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
#elif defined(ARDUINO_M5STACK_ATOM) || defined(ARDUINO_M5Stack_ATOM)
    #define GROVE1_PIN 26
    #define GROVE2_PIN 32
    #define NEO_PIN 27
#elif defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_PLUS)
    #define GROVE1_PIN 32
    #define GROVE2_PIN 33
    #define NEO_PIN 26 // doesn't have one; this pin is just unused
#else
    #error undefined hardware
#endif

void setup(void) {
    M5.begin();
    Serial.begin(115200);
    FastLED.addLeds<WS2811, GROVE1_PIN, GRB>(lstrip, lstrip_count);
    FastLED.addLeds<WS2811, GROVE2_PIN, GRB>(rstrip, rstrip_count);
    FastLED.addLeds<WS2811, NEO_PIN, GRB>(btnled, 1);
    allstrips.fill(CRGB::Black);
    FastLED.show();

    ansys.addAnimation(&doubleCrawlAnim);

    commsSetup();

    if(M5.getDisplayCount() > 0)
    {
        displaySetup(M5.getDisplay(0));
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
    commsUpdate();

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
    logger.print("New mode: ");
    logger.println(localPrefs.mode);
    buttonled.fill(localPrefs.mode==0 ? CRGB::Black : runMode==1 ? localPrefs.mainColor : localPrefs.secondaryColor);

    if(runMode == Off) {
        FastLED.setBrightness(0);
    } else {
        FastLED.setBrightness(brightnessProp.get().toInt());

        ansys.removeAnimation(0);
        StripAnimation *anim = animations[(int)localPrefs.mode-1];
        anim->prefs = &localPrefs;
        anim->duration = anim->prefs->speed;
        ansys.addAnimation(anim);
    }
}

void update(void)
{
    if(M5.BtnA.wasPressed())
    {
        RunMode newMode = (RunMode)((localPrefs.mode + 1) % (animations.size()+1));
        String modeStr = String((int)newMode);
        modeProp.set(modeStr);
    }
}
