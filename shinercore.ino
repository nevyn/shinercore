#include "M5Unified.h"
#include "FastLED.h"
#include <OverAnimate.h>
#include <SubStrip.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

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
BLEServer *server = NULL;

#define kAllCharacteristicProperties BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE

#define kServiceUUID        "6c0de004-629d-4717-bed5-847fddfbdc2e"
BLEService* service = NULL;

#define kSpeedCharacteristicUUID "5341966c-da42-4b65-9c27-5de57b642e28"
struct SpeedCharacteristic : public BLECharacteristicCallbacks
{
    BLECharacteristic *chara;
    void onRead(BLECharacteristic *chara)
    {
        Serial.println("Hello reading duration");
        chara->setValue(doubleCrawlAnim.duration);
    }
    
    void onWrite(BLECharacteristic *chara)
    {
        std::string val = chara->getValue();
        Serial.print("Hello reading duration");
        Serial.println(val.c_str());
        doubleCrawlAnim.duration = std::stof(val);
    }
};

SpeedCharacteristic speedChara;

void commsSetup()
{
    Serial.begin(115200);

    BLEDevice::init("shinercore");
    server = BLEDevice::createServer();
    service = server->createService(kServiceUUID);
    speedChara.chara = service->createCharacteristic(kSpeedCharacteristicUUID, kAllCharacteristicProperties);
    speedChara.chara->addDescriptor(new BLE2902());

    service->start();
    BLEAdvertising *ad = server->getAdvertising();
    ad->start();
}


///// Runtime things

void setup() {
    M5.begin();
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
