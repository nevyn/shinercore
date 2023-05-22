#include "M5Unified.h"
#include "FastLED.h"
#include <OverAnimate.h>
#include <SubStrip.h>
#include <ArduinoBLE.h>
#include <Preferences.h>

////// Animation things

enum RunMode
{
    Off = 0,
    DoubleCrawl = 1,

    RunModeCount
} runMode;

static const int lstrip_count = 400;
CRGB lstrip[lstrip_count];
SubStrip left(lstrip, lstrip_count);
static const int rstrip_count = 400;
CRGB rstrip[rstrip_count];
SubStrip right(rstrip, rstrip_count);
ProxyStrip allstrips({&left, &right});

CRGB btnled[1];
SubStrip buttonled(btnled, 1);

CRGB mainColor(255, 100, 0);
CRGB secondaryColor(240, 255, 0);

float p_tau = 10.0;
float p_phi = 4.0;

String ownerName = "unknown";

AnimationSystem ansys;

typedef void(*StripFunc)(Animation*, SubStrip *strip, float);
class StripAnimation : public Animation
{
public:
    StripFunc func;
    SubStrip *strip;
    StripAnimation(StripFunc func, SubStrip *strip, TimeInterval duration = 1.0, bool repeats = false) 
      : Animation(duration, repeats), func(func), strip(strip) {}
protected:
    void animate(float absoluteTime) { func(this, strip, absoluteTime); }
};

float frand(void)
{
  return random(1000)/1000.0;
}

float curve(float progress)
{
    return sin(progress*6.28f)/2.0f + 0.5f;
}

void DoubleCrawlAnim(Animation *self, SubStrip *strip, float t)
{
    for(int i = 0; i < strip->numPixels(); i++)
    {
        strip->set(i, mainColor * (gammaf(curve(t - i/p_tau))/2.0f) + secondaryColor * (gammaf(curve(t + i/p_phi))/2.0f));
    }
}
StripAnimation doubleCrawlAnim(DoubleCrawlAnim, &allstrips, 2.0, true);

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
        Serial.print(key); Serial.print(" loaded initial value "); Serial.println(value);
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
            Serial.println("failed to store preferences!");
            while (1);
        }
        Serial.print(key); Serial.print(" saved new value "); Serial.println(value);
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
    doubleCrawlAnim.duration = newValue.toFloat();
});
StoredProperty colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color1", "255 100 0", "0 0 0,255 255 255", [](const String &newValue) {
    mainColor = rgbFromString(newValue);
    if (runMode == 1) buttonled.fill(mainColor);
});
StoredProperty color2Prop("83595a76-1b17-4158-bcee-e702c3165caf", "color2", "240 255 0", "0 0 0,255 255 255", [](const String &newValue) {
    secondaryColor = rgbFromString(newValue);
});
StoredProperty modeProp("70d4cabe-82cc-470a-a572-95c23f1316ff", "mode", "1", "0,1", [](const String &newValue) {
    setMode((RunMode)newValue.toInt());
});
StoredProperty brightnessProp("2B01", "brightness", "255", "0-255", [](const String &newValue) {
    FastLED.setBrightness(newValue.toInt());
});
StoredProperty tauProp("d879c81a-09f0-4a24-a66c-cebf358bb97a", "tau", "10.0", "-100.0,100.0", [](const String &newValue) {
    p_tau = newValue.toFloat();
});
StoredProperty phiProp("df6f0905-09bd-4bf6-b6f5-45b5a4d20d52", "phi", "4.0", "-100.0,100.0", [](const String &newValue) {
    p_phi = newValue.toFloat();
});
StoredProperty nameProp("7ad50f2a-01b5-4522-9792-d3fd4af5942f", "name", "unknown", "", [](const String &newValue) {
    ownerName = newValue;
});

std::array<StoredProperty*, 8> props = {&speedProp, &colorProp, &color2Prop, &modeProp, &brightnessProp, &tauProp, &phiProp, &nameProp};


void commsSetup()
{
    if (!prefs.begin("shinercore"))
    {
        Serial.println("failed to read preferences!");
        while (1);
    }

    if (!BLE.begin()) {
        Serial.println("starting Bluetooth® Low Energy module failed!");
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
    BLE.advertise();
}

void commsUpdate()
{
    BLE.poll();

    for(const auto& prop: props)
    {
        prop->poll();
    }
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

void setup() {
    M5.begin();
    Serial.begin(115200);
    FastLED.addLeds<WS2811, GROVE1_PIN, GRB>(lstrip, lstrip_count);
    FastLED.addLeds<WS2811, GROVE2_PIN, GRB>(rstrip, rstrip_count);
    FastLED.addLeds<WS2811, NEO_PIN, GRB>(btnled, 1);
    allstrips.fill(CRGB::Black);
    FastLED.show();

    ansys.addAnimation(&doubleCrawlAnim);

    commsSetup();
}

unsigned long lastMillis;
void loop() {
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
}

void setMode(RunMode newMode)
{
    runMode = newMode;
    Serial.print("New mode: ");
    Serial.println(runMode);
    buttonled.fill(runMode==0 ? CRGB::Black : runMode==1 ? mainColor : secondaryColor);

    if(runMode == Off) {
        FastLED.setBrightness(0);
    } else {
        FastLED.setBrightness(brightnessProp.get().toInt());
    }
}

void update()
{
    if(M5.BtnA.wasPressed())
    {
        RunMode newMode = (RunMode)((runMode + 1) % RunModeCount);
        String modeStr = String((int)newMode);
        modeProp.set(modeStr);
    }
}
