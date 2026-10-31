BLEService shinerService("6c0de004-629d-4717-bed5-847fddfbdc2e");

// Documentation characteristic - returns JSON with available blend modes and animations
BLEStringCharacteristic documentationChara("76db9199-21af-4207-a23c-dc138a6cd42d", BLERead, 512);
BLEDescriptor documentationNameDescriptor(kDescriptorUserDesc, "documentation");
BLEStringCharacteristic animationDocsChara("5a9ce2c0-4837-4bd4-825f-ae639fdccc31", BLERead, 512);
BLEDescriptor animationDocsNameDescriptor(kDescriptorUserDesc, "animationDocs");

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
void applyLayerMove(int from, int to);

// global settings
GlobalProperty<RunMode> modeProp("70d4cabe-82cc-470a-a572-95c23f1316ff", "mode", &localPrefs.mode, Off, On);
GlobalProperty<int> brightnessProp("2B01", "brightness", &localPrefs.brightness, 0, 255);
GlobalProperty<String> nameProp("7ad50f2a-01b5-4522-9792-d3fd4af5942f", "name", &ownerName);
GlobalProperty<int> layerProp("0a7eadd8-e4b8-4384-8308-e67a32262cc4", "layer", &localPrefs.currentLayerIndex, 0, LAYER_COUNT-1, republishLayers);
GlobalProperty<int> presetProp("8b989f5e-3d22-4377-80c9-c54eeb459518", "preset", &localPrefs.currentPresetIndex, 0, PRESET_COUNT-1, loadLayers);
GlobalProperty<int> bootPresetProp("ffc4df79-8c9c-4efc-8d8f-867b2a776717", "bootPreset", &localPrefs.bootPreset, -1, PRESET_COUNT-1);
GlobalProperty<int> ledCountProp("f5c67dcb-8798-4818-901f-cff9917d1a62", "ledCount", &localPrefs.ledCount, 0, MAX_LED_COUNT);
GlobalProperty<int> maxCurrentProp("2b6d7f98-2df7-446d-a0df-112e34d202f5", "maxCurrent", &localPrefs.maxCurrentMa, 0, 10000);
GlobalProperty<LedColorOrder> ledColorOrderProp("f3b7c8a1-5d2e-4f19-8c6a-9e1d0b2c3a4f", "ledColorOrder", &localPrefs.ledColorOrder);
GlobalProperty<int> micProp("519f61ae-bb92-425f-90fa-29aabc63520d", "mic", &localPrefs.micEnabled, 0, 1);
GlobalProperty<int> meshProp("808fb660-9eba-45c2-862c-be18a55195ed", "mesh", &localPrefs.meshEnabled, 0, 1);
GlobalProperty<int> meshShowProp("f4d7b6bd-aeb2-4ea1-a5fc-254e3db10200", "meshShow", &localPrefs.meshShow, 0, 1);
void meshGroupChanged(); // defined in the sketch, next to the mesh instance
GlobalProperty<String> meshGroupProp("12239682-6d25-45d1-a3b5-182dfa6cf106", "meshGroup", &localPrefs.meshGroup, meshGroupChanged);
GlobalProperty<int> carouselBeatsProp("1bc3f537-b285-4def-a2f9-285dae0b2aaa", "carouselBeats", &localPrefs.carouselBeats, 1, 64);
WiringProperty wiringProp("2be57867-0fd5-45b7-93af-c9edff69fe60", "wiring", &localPrefs.wiring);
PinStatusProperty pinStatusProp("53775889-c119-4673-89f2-359a80fabcca", "pinStatus");
LayerMoveProperty layerMoveProp("df64df61-5728-4e42-809f-bb7735fb30a0", "layerMove", applyLayerMove);
LayerSummaryProperty layerSummaryProp("9231bed4-aec8-4fa0-88be-3242e937fc0a", "a988b500-1b73-48e7-b353-9612e873af48", "4f577ef4-d444-4b85-93a0-4b6a4d4c0e49");
BLEStringCharacteristic wiringDocsChara("682bf183-44f6-4dc9-9eb0-640c05ae45db", BLERead, 512);
BLEDescriptor wiringDocsNameDescriptor(kDescriptorUserDesc, "wiringDocs");

// per-layer settings
LayerProperty<float> speedProp("5341966c-da42-4b65-9c27-5de57b642e28", "speed", &ShinyLayerSettings::speed, 0.001f, 100.0f);
LayerProperty<CRGB> colorProp("c116fce1-9a8a-4084-80a3-b83be2fbd108", "color1", &ShinyLayerSettings::mainColor);
LayerProperty<CRGB> color2Prop("83595a76-1b17-4158-bcee-e702c3165caf", "color2", &ShinyLayerSettings::secondaryColor);
LayerProperty<float> tauProp("d879c81a-09f0-4a24-a66c-cebf358bb97a", "tau", &ShinyLayerSettings::p_tau, -100.0f, 100.0f);
LayerProperty<float> phiProp("df6f0905-09bd-4bf6-b6f5-45b5a4d20d52", "phi", &ShinyLayerSettings::p_phi, -100.0f, 100.0f);
LayerProperty<LayerBlendMode> blendModeProp("03686c5c-6e6f-44f0-943f-db6388d9fdd4", "blendMode", &ShinyLayerSettings::blendMode);
LayerProperty<int, AnimationCodec> animationProp("bee29c30-aa11-45b2-b5a2-8ff8d0bab262", "animation", &ShinyLayerSettings::animationIndex);
LayerProperty<int> beatSyncProp("6f97efc2-096e-4704-9feb-f9c2f41577ee", "beatSync", &ShinyLayerSettings::beatSync, 0, 1);
LayerProperty<float> opacityProp("721da9f2-34da-4da2-bb64-0de65be811db", "opacity", &ShinyLayerSettings::opacity, 0.0f, 1.0f);

std::vector<GlobalPropertyBase*> globalProps = {&modeProp, &brightnessProp, &nameProp, &layerProp, &presetProp, &bootPresetProp, &ledColorOrderProp, &ledCountProp, &maxCurrentProp, &micProp, &meshProp, &meshShowProp, &meshGroupProp, &carouselBeatsProp, &wiringProp};
std::vector<LayerPropertyBase*> layerProps = {&speedProp, &colorProp, &color2Prop, &tauProp, &phiProp, &animationProp, &blendModeProp, &beatSyncProp, &opacityProp};
std::vector<Property*> props = [&] {
    std::vector<Property*> v;
    v.reserve(globalProps.size() + layerProps.size() + 3);
    v.insert(v.end(), globalProps.begin(), globalProps.end());
    v.insert(v.end(), layerProps.begin(), layerProps.end());
    v.push_back(&pinStatusProp);
    v.push_back(&layerSummaryProp);
    v.push_back(&layerMoveProp);
    return v;
}();

// Pull every layer of the current preset out of NVS (or its pending writes) into
// localPrefs, and republish the cursor layer's values to bluetooth
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

// One edit of the current preset: the mesh shares it like any change to localPrefs.layers
void applyLayerMove(int from, int to)
{
    if(from == to) return;
    ShinyLayerSettings before[LAYER_COUNT];
    std::copy(localPrefs.layers, localPrefs.layers + LAYER_COUNT, before);
    moveLayer(localPrefs.layers, from, to);
    for(const auto& prop: layerProps)
    {
        prop->persistChanged(before);
    }
    if(localPrefs.currentLayerIndex == from) layerProp.set(to); // republishes via its hook
    else republishLayers();
}

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
    // An installation that loses power nightly comes back on with the same preset every morning
    if(localPrefs.bootPreset >= 0)
    {
        if(localPrefs.mode != On) modeProp.set(On);
        if(localPrefs.currentPresetIndex != localPrefs.bootPreset) presetProp.set(localPrefs.bootPreset);
    }
    // No central yet, so these can't block; the GATT store must be populated
    // before advertising rather than trickling out through the flush limiter
    for(const auto& prop: props)
    {
        prop->flushPublish();
    }

    // Add documentation characteristic (read-only, not stored)
    documentationChara.addDescriptor(documentationNameDescriptor);
    shinerService.addCharacteristic(documentationChara);
    documentationChara.writeValue(buildDocumentationJSON());
    animationDocsChara.addDescriptor(animationDocsNameDescriptor);
    shinerService.addCharacteristic(animationDocsChara);
    animationDocsChara.writeValue(animationDocsJSON());
    wiringDocsChara.addDescriptor(wiringDocsNameDescriptor);
    shinerService.addCharacteristic(wiringDocsChara);
    wiringDocsChara.writeValue(wiringDocsJSON(ACTIVE_BOARD));

    String name = ownerName + "'s shinercore";
    BLE.setDeviceName(name.c_str());
    BLE.setLocalName(name.c_str());

    BLE.setAdvertisedService(shinerService);
    BLE.addService(shinerService);
    if (BLE.advertise()) {
        logger.println("Advertising local shiner service");
    } else {
        logger.println("Failed to advertise");
    }
}

void commsUpdate(TimeInterval delta)
{
    BLE.poll();

    static bool centralWasConnected = false;
    BLEDevice central = BLE.central();
    if((bool)central != centralWasConnected)
    {
        centralWasConnected = central;
        logger.print("app "); logger.println(central ? "connected" : "disconnected");
    }

    for(const auto& prop: props)
    {
        prop->poll();
        prop->update(delta);
    }
    static size_t nextPersist = 0;
    flushOneSettledPersist(props, nextPersist);

    // One staged publish per 50ms, round-robin: a burst (the cursor fanout's
    // 8, a slider drag's stream) spaced out never exhausts the controller's
    // ACL credits, which ArduinoBLE busy-waits on — against a stalling link
    // that wait wedged the whole loop (see docs/ble-comms.md).
    // Cursor echoes jump the queue: the app must learn what the per-layer
    // characteristics address before their re-published values reach it.
    static TimeInterval sinceFlush = 1;
    static size_t nextFlush = 0;
    sinceFlush += delta;
    if(sinceFlush >= 0.05)
    {
        if(presetProp.flushPublish() || layerProp.flushPublish())
        {
            sinceFlush = 0;
        }
        else for(size_t i = 0; i < props.size(); i++)
        {
            if(props[(nextFlush + i) % props.size()]->flushPublish())
            {
                nextFlush = (nextFlush + i + 1) % props.size();
                sinceFlush = 0;
                break;
            }
        }
    }
}
