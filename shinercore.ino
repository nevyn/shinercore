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
        (*strip)[i] = CRGB(255, 100, 0) * (gammaf(curve(t - i/10.0f))/2.0f) + CRGB(240, 255, 0) * (gammaf(curve(t + i/4.0f))/2.0f);
    }
}
StripAnimation doubleCrawlAnim(DoubleCrawlAnim, &left, 0.5, true);

////// Communication things

Preferences prefs;

class StoredProperty 
{
public:
    StoredProperty(const char *uuid, const char *key, String defaultValue, std::function<void(String&)> applicator)
      : chara(uuid, BLERead | BLEWrite, 36),
        key(key),
        value(defaultValue),
        applicator(applicator)
    {
        load();
    }
    void set(String &newVal)
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
protected:
    void load()
    {
        value = prefs.getString(key, value);
        chara.writeValue(value);
        applicator(value);
        Serial.print(key); Serial.print(" loaded initial value "); Serial.println(value);
    }
    void save()
    {
        prefs.putString(key, value);
        Serial.print(key); Serial.print(" saved new value "); Serial.println(value);
    }
protected:
    BLEStringCharacteristic chara;
    const char *key;
    String value;
    std::function<void(String&)> applicator;
};

BLEService shinerService("6c0de004-629d-4717-bed5-847fddfbdc2e");
StoredProperty speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", "0.5", [](String &newValue) {
    doubleCrawlAnim.duration = newValue.toFloat();
});
StoredProperty colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color", "0,255,0", [](String &newValue) {
    
});

std::array<StoredProperty*, 2> props = {&speedProp, &colorProp};


void commsSetup()
{
    prefs.begin("shinercore");

    if (!BLE.begin()) {
        Serial.println("starting Bluetooth® Low Energy module failed!");
        while (1);
    }
    BLE.setDeviceName("shinercore");
    BLE.setLocalName("shinercore");

    for(const auto& prop: props)
    {
        prop->advertise(shinerService);
    }
    
    BLE.setAdvertisedService(shinerService);
    BLE.addService(shinerService);
    BLE.advertise();
}

void commsLoop()
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

    setMode(DoubleCrawl);
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

    ansys.playElapsedTime(delta);
    FastLED.show();

    commsLoop();
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
        setMode((RunMode)((runMode + 1) % RunModeCount));
    }
}
