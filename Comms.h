BLEService shinerService("6c0de004-629d-4717-bed5-847fddfbdc2e");

// global settings
StoredProperty modeProp("70d4cabe-82cc-470a-a572-95c23f1316ff", "mode", "1", "0,1", [](const String &newValue) {
    setMode((RunMode)newValue.toInt());
});
StoredProperty brightnessProp("2B01", "brightness", "255", "0-255", [](const String &newValue) {
    FastLED.setBrightness(newValue.toInt());
});
StoredProperty nameProp("7ad50f2a-01b5-4522-9792-d3fd4af5942f", "name", "unknown", "", [](const String &newValue) {
    ownerName = newValue;
});
StoredProperty layerProp("0a7eadd8-e4b8-4384-8308-e67a32262cc4", "name", "unknown", "", [](const String &newValue) {
    // ...
});

// per-layer settings
StoredMultiProperty speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", "0.5", "0.0,100.0", [](const String &newValue) {
    localPrefs.speed = newValue.toFloat();
    for(const auto& anim: animations)
    {
        anim->duration = localPrefs.speed;
    }
});
StoredMultiProperty colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color1", "255 100 0", "0 0 0,255 255 255", [](const String &newValue) {
    localPrefs.mainColor = rgbFromString(newValue);
    if (localPrefs.mode == 1) buttonled.fill(localPrefs.mainColor);
});
StoredMultiProperty color2Prop("83595a76-1b17-4158-bcee-e702c3165caf", "color2", "240 255 0", "0 0 0,255 255 255", [](const String &newValue) {
    localPrefs.secondaryColor = rgbFromString(newValue);
});

StoredMultiProperty tauProp("d879c81a-09f0-4a24-a66c-cebf358bb97a", "tau", "10.0", "-100.0,100.0", [](const String &newValue) {
    localPrefs.p_tau = newValue.toFloat();
});
StoredMultiProperty phiProp("df6f0905-09bd-4bf6-b6f5-45b5a4d20d52", "phi", "4.0", "-100.0,100.0", [](const String &newValue) {
    localPrefs.p_phi = newValue.toFloat();
});

StoredMultiProperty animationProp("bee29c30-aa11-45b2-b5a2-8ff8d0bab262", "name", "unknown", "", [](const String &newValue) {
    // ...
});
std::array<StoredProperty*, 10> props = {&speedProp, &colorProp, &color2Prop, &modeProp, &brightnessProp, &tauProp, &phiProp, &nameProp, &layerProp, &animationProp};

class RemoteCore
{
public:
    RemoteCore(BLEDevice device) :
        device(device),
        connected(false),
        failed(false),
        retryDuration(1),
        untilNextRetry(0)
    {}

    BLEDevice device;
    ShinySettings prefs;
    bool connected;
    bool failed;
    TimeInterval untilNextRetry;
    TimeInterval retryDuration;

    void fail()
    {
        untilNextRetry = retryDuration;
        retryDuration = std::min(retryDuration*2, 60.0);
        logger.printf("Retrying in %.2f...\n", untilNextRetry);
        failed = true;
        connected = false;
        if(device && device.connected())
        {
            device.disconnect();
        }
    }

    void elapseDelta(TimeInterval delta)
    {
        untilNextRetry -= delta;
        if(untilNextRetry <= 0)
        {
            logger.printf("Retrying!\n");
            connect();
        }
    }

    void connect()
    {
        logger.printf("Connecting to %s...\n", this->device.localName().c_str());
        if(!this->device.connect())
        {
            this->fail();
            logger.printf("Failed to connect :'(\n");
            return;
        }
        this->connected = true;
    
        logger.printf("Connected!\n");
    
        if(!this->device.discoverService(shinerService.uuid()))
        {
            logger.printf("Failed to discover shiner service\n");
            this->fail();
            return;
        }
    
        BLECharacteristic mainColor = this->device.characteristic(colorProp.uuid());
        if(mainColor)
        {
            char colorStr[255];
            // see also: characteristic.valueUpdated()
            mainColor.readValue(colorStr, 255);
            logger.printf("That core has primary color %s\n", colorStr);
        } else {
            logger.printf("Booo, can't read its color prop :(\n");
        }
    }
};
std::vector<RemoteCore*> remoteCores;

bool doAdvertise = true;
bool doFindRemoteCores = false;

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
    
    if (doAdvertise)
    {
        BLE.setAdvertisedService(shinerService);
        BLE.addService(shinerService);
        if (BLE.advertise()) {
            logger.println("Advertising local shiner service");
        } else {
            logger.println("Failed to advertise");
        }
    }

    // scan for other shinercores
    if (doFindRemoteCores)
    {
        BLE.scanForUuid(shinerService.uuid());
    }
}

void remoteCoreFound(BLEDevice foundDevice)
{
    RemoteCore *remoteCore = new RemoteCore(foundDevice);
    remoteCores.push_back(remoteCore);

    remoteCore->connect();
}

void commsUpdate(TimeInterval delta)
{
    BLE.poll();

    for(const auto& prop: props)
    {
        prop->poll();
    }

    if (doFindRemoteCores)
    {
        BLEDevice foundDevice = BLE.available();
        auto containsFoundDevice = [foundDevice](RemoteCore *icore) {
            return icore->device == foundDevice;
        };
        if(foundDevice && std::find_if(remoteCores.begin(), remoteCores.end(), containsFoundDevice) == remoteCores.end())
        {
            // can't connect while scanning
            BLE.stopScan();
    
            // Query and insert into local state
            remoteCoreFound(foundDevice);
    
            // all done connecting, keep scanning
            BLE.scanForUuid(shinerService.uuid());
        }
    }

    for(auto it = remoteCores.begin(); it != remoteCores.end(); ++it)
    {
        RemoteCore *remoteCore = *it;
        if(remoteCore->connected)
        {
            remoteCore->device.poll();
            if(!remoteCore->device.connected())
            {
                logger.printf("Lost connection to %s.\n", remoteCore->device.localName().c_str());
                it = remoteCores.erase(it);
            }
        }
        else
        {
            remoteCore->elapseDelta(delta);
        }
    }
}
