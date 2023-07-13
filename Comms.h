BLEService shinerService("6c0de004-629d-4717-bed5-847fddfbdc2e");

StoredProperty speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", "0.5", "0.0,100.0", [](const String &newValue) {
    localPrefs.speed = newValue.toFloat();
    for(const auto& anim: animations)
    {
        anim->duration = localPrefs.speed;
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

class RemoteCore
{
public:
    RemoteCore(BLEDevice device) :
        device(device),
        connected(false),
        failed(false)
    {}

    BLEDevice device;
    ShinySettings prefs;
    bool connected;
    bool failed;
};
std::vector<RemoteCore*> remoteCores;

bool doAdvertise = true;

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
    BLE.scanForUuid(shinerService.uuid());
}

void remoteCoreConnected(BLEDevice foundDevice)
{
    RemoteCore *remoteCore = new RemoteCore(foundDevice);
    remoteCores.push_back(remoteCore);

    logger.printf("Connecting to %s...\n", remoteCore->device.localName().c_str());
    if(!remoteCore->device.connect())
    {
        remoteCore->failed = true;
        logger.printf("Failed to connect :'(\n");
        return;
    }
    remoteCore->connected = true;

    logger.printf("Connected!\n");

    if(!remoteCore->device.discoverService(shinerService.uuid()))
    {
        logger.printf("Failed to discover shiner service\n");
        remoteCore->failed = true;
        remoteCore->connected = false;
        return;
    }

    BLECharacteristic mainColor = remoteCore->device.characteristic(colorProp.uuid());
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

void commsUpdate(void)
{
    BLE.poll();

    for(const auto& prop: props)
    {
        prop->poll();
    }

    BLEDevice foundDevice = BLE.available();
    auto containsFoundDevice = [foundDevice](RemoteCore *icore) {
        return icore->device == foundDevice;
    };
    if(foundDevice && std::find_if(remoteCores.begin(), remoteCores.end(), containsFoundDevice) == remoteCores.end())
    {
        // can't connect while scanning
        BLE.stopScan();

        // Query and insert into local state
        remoteCoreConnected(foundDevice);

        // all done connecting, keep scanning
        BLE.scanForUuid(shinerService.uuid());
    }
}
