BLEService shinerService("6c0de004-629d-4717-bed5-847fddfbdc2e");

// Documentation characteristic - returns JSON with available blend modes and animations
BLEStringCharacteristic documentationChara("76db9199-21af-4207-a23c-dc138a6cd42d", BLERead, 512);
BLEDescriptor documentationNameDescriptor(kDescriptorUserDesc, "documentation");

String buildDocumentationJSON() {
    String json = "{\"blendModes\":[";
    for(size_t i = 0; i < blendModeNames.size(); i++) {
        if(i > 0) json += ",";
        json += "\"" + blendModeNames[i] + "\"";
    }
    json += "],\"animations\":[";
    for(size_t i = 0; i < animationNames.size(); i++) {
        if(i > 0) json += ",";
        json += "\"" + animationNames[i] + "\"";
    }
    json += "],\"ledColorOrders\":[";
    for(size_t i = 0; i < ledColorOrderNames.size(); i++) {
        if(i > 0) json += ",";
        json += "\"" + ledColorOrderNames[i] + "\"";
    }
    json += "]}";
    return json;
}

// Defaults are the ShinySettings/ShinyLayerSettings initializers; ranges are
// enforced on write. The layer and preset properties are the cursor that
// selects which layer the per-layer characteristics address.
void loadLayers();
void republishLayers();

// global settings
GlobalProperty<RunMode> modeProp("70d4cabe-82cc-470a-a572-95c23f1316ff", "mode", &localPrefs.mode, Off, On);
GlobalProperty<int> brightnessProp("2B01", "brightness", &localPrefs.brightness, 0, 255);
GlobalProperty<String> nameProp("7ad50f2a-01b5-4522-9792-d3fd4af5942f", "name", &ownerName);
GlobalProperty<int> layerProp("0a7eadd8-e4b8-4384-8308-e67a32262cc4", "layer", &localPrefs.currentLayerIndex, 0, LAYER_COUNT-1, republishLayers);
GlobalProperty<int> presetProp("8b989f5e-3d22-4377-80c9-c54eeb459518", "preset", &localPrefs.currentPresetIndex, 0, PRESET_COUNT-1, loadLayers);
GlobalProperty<int> ledCountProp("f5c67dcb-8798-4818-901f-cff9917d1a62", "ledCount", &localPrefs.ledCount, 0, MAX_LED_COUNT);
GlobalProperty<LedColorOrder> ledColorOrderProp("f3b7c8a1-5d2e-4f19-8c6a-9e1d0b2c3a4f", "ledColorOrder", &localPrefs.ledColorOrder);
GlobalProperty<int> micProp("519f61ae-bb92-425f-90fa-29aabc63520d", "mic", &localPrefs.micEnabled, 0, 1);

// per-layer settings
LayerProperty<float> speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", &ShinyLayerSettings::speed, 0.001f, 100.0f);
LayerProperty<CRGB> colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color1", &ShinyLayerSettings::mainColor);
LayerProperty<CRGB> color2Prop("83595a76-1b17-4158-bcee-e702c3165caf", "color2", &ShinyLayerSettings::secondaryColor);
LayerProperty<float> tauProp("d879c81a-09f0-4a24-a66c-cebf358bb97a", "tau", &ShinyLayerSettings::p_tau, -100.0f, 100.0f);
LayerProperty<float> phiProp("df6f0905-09bd-4bf6-b6f5-45b5a4d20d52", "phi", &ShinyLayerSettings::p_phi, -100.0f, 100.0f);
LayerProperty<LayerBlendMode> blendModeProp("03686c5c-6e6f-44f0-943f-db6388d9fdd4", "blendMode", &ShinyLayerSettings::blendMode);
LayerProperty<int, AnimationCodec> animationProp("bee29c30-aa11-45b2-b5a2-8ff8d0bab262", "animation", &ShinyLayerSettings::animationIndex);
LayerProperty<int> beatSyncProp("6f97efc2-096e-4704-9feb-f9c2f41577ee", "beatSync", &ShinyLayerSettings::beatSync, 0, 1);

std::vector<GlobalPropertyBase*> globalProps = {&modeProp, &brightnessProp, &nameProp, &layerProp, &presetProp, &ledColorOrderProp, &ledCountProp, &micProp};
std::vector<LayerPropertyBase*> layerProps = {&speedProp, &colorProp, &color2Prop, &tauProp, &phiProp, &animationProp, &blendModeProp, &beatSyncProp};
std::vector<Property*> props = [&] {
    std::vector<Property*> v;
    v.reserve(globalProps.size() + layerProps.size());
    v.insert(v.end(), globalProps.begin(), globalProps.end());
    v.insert(v.end(), layerProps.begin(), layerProps.end());
    return v;
}();

// Pull every layer of the current preset out of NVS into localPrefs, and
// republish the cursor layer's values to bluetooth
void loadLayers()
{
    for(const auto& prop: layerProps)
    {
        for(int layer = 0; layer < LAYER_COUNT; layer++)
        {
            prop->load(layer, localPrefs.currentPresetIndex);
        }
        prop->republish();
    }
}

void republishLayers()
{
    for(const auto& prop: layerProps)
    {
        prop->republish();
    }
}

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

    if(M5.BtnA.isHolding()) {
        logger.println("CLEARING SETTINGS DUE TO BUTTON HELD\n");
        prefs.clear();
    }

    migrateSettings();

    if (!BLE.begin()) {
        logger.println("starting Bluetooth® Low Energy module failed!");
        while (1);
    }

    for(const auto& prop: props)
    {
        prop->advertise(shinerService);
    }
    // presetProp's load pulls in every layer's settings via its loadLayers hook
    for(const auto& prop: globalProps)
    {
        prop->load();
    }

    // Add documentation characteristic (read-only, not stored)
    documentationChara.addDescriptor(documentationNameDescriptor);
    shinerService.addCharacteristic(documentationChara);
    documentationChara.writeValue(buildDocumentationJSON());

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

    for(auto it = remoteCores.begin(); it != remoteCores.end();)
    {
        RemoteCore *remoteCore = *it;
        if(remoteCore->connected)
        {
            remoteCore->device.poll();
            if(!remoteCore->device.connected())
            {
                logger.printf("Lost connection to %s.\n", remoteCore->device.localName().c_str());
                delete remoteCore;
                it = remoteCores.erase(it);
                continue;
            }
        }
        else
        {
            remoteCore->elapseDelta(delta);
        }
        ++it;
    }
}
