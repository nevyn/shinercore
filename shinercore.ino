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

static const int lstrip_count = 80;
CRGB lstrip[lstrip_count];
SubStrip left(lstrip, lstrip_count);

CRGB btnled[1];
SubStrip buttonled(btnled, 1);

CRGB mainColor(255, 100, 0);
CRGB secondaryColor(240, 255, 0);

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
        (*strip)[i] = mainColor * (gammaf(curve(t - i/10.0f))/2.0f) + secondaryColor * (gammaf(curve(t + i/4.0f))/2.0f);
    }
}
StripAnimation doubleCrawlAnim(DoubleCrawlAnim, &left, 2.0, true);

////// Communication things

#define kDescriptorUserDesc "2901"
#define kDescriptorClientCharacteristic "2902"
#define kDescriptorServerCharacteristic "2903"
#define kDescriptorPresentationFormat "2904"
#define kDescriptorPresentationFormat_S32 "10"
#define kDescriptorPresentationFormat_Float32 "14"
#define kDescriptorPresentationFormat_String "19"

Preferences prefs;
class StoredProperty 
{
public:
    StoredProperty(const char *uuid, const char *key, String defaultValue, std::function<void(const String&)> applicator)
      : chara(uuid, BLERead | BLEWrite, 36),
        key(key),
        value(defaultValue),
        applicator(applicator),
        nameDescriptor(kDescriptorUserDesc, key),
        formatDescriptor(kDescriptorPresentationFormat, kDescriptorPresentationFormat_String)
    {
        chara.addDescriptor(nameDescriptor);
        chara.addDescriptor(formatDescriptor);
    }
    void set(const String &newVal)
    {
        value = newVal;
        chara.writeValue(newVal);
        save();
        applicator(newVal);
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
        if (prefs.putString(key, value) == 0)
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
    std::function<void(const String&)> applicator;

    BLEDescriptor nameDescriptor;
    BLEDescriptor formatDescriptor;
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

StoredProperty speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", "0.5", [](const String &newValue) {
    doubleCrawlAnim.duration = newValue.toFloat();
});
StoredProperty modeProp("70d4cabe-82cc-470a-a572-95c23f1316ff", "mode", "1", [](const String &newValue) {
    setMode((RunMode)newValue.toInt());
});
StoredProperty colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color1", "255 100 0", [](const String &newValue) {
    mainColor = rgbFromString(newValue);
});
StoredProperty color2Prop("83595a76-1b17-4158-bcee-e702c3165caf", "color2", "240 255 0", [](const String &newValue) {
    secondaryColor = rgbFromString(newValue);
});

std::array<StoredProperty*, 4> props = {&speedProp, &modeProp, &colorProp, &color2Prop};


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
    BLE.setDeviceName("shinercore");
    BLE.setLocalName("shinercore");

    for(const auto& prop: props)
    {
        prop->load();
        prop->advertise(shinerService);
    }
    
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

void setup() {
    M5.begin();
    Serial.begin(115200);
    FastLED.addLeds<WS2811, 2, GRB>(lstrip, lstrip_count);
    FastLED.addLeds<WS2811, 35, GRB>(btnled, 1);
    left.fill(CRGB::Black);
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
    buttonled.fill(runMode==0 ? CRGB::Black : runMode==1 ? CRGB::Red : CRGB::Green);

    if(runMode == Off) {
        FastLED.setBrightness(0);
    } else {
        FastLED.setBrightness(255);
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
