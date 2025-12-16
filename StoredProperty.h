
#define kDescriptorUserDesc "2901"
#define kDescriptorClientCharacteristic "2902"
#define kDescriptorServerCharacteristic "2903"
#define kDescriptorPresentationFormat "2904"
#define kDescriptorPresentationFormat_S32 "10"
#define kDescriptorPresentationFormat_Float32 "14"
#define kDescriptorPresentationFormat_String "19"
#define kDescriptorValidRange "2906"

// Stores a setting under a specific string key name, and also publishes that setting over bluetooth with a characteristic UUID. Calls an 
// "applicator" whenever the value is changed either by reading from disk or from changing by bluetooth command; implement this function to 
// apply this setting in business logic.
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
    virtual void load()
    {
        String curKey = currentKey();
        value = prefs.getString(curKey.c_str(), defaultValue);
        chara.writeValue(value);
        applicator(value);
        logger.print(curKey); logger.print(" := "); logger.println(value);
    }
    void writeToChara()
    {
        String curKey = currentKey();
        value = prefs.getString(curKey.c_str(), defaultValue);
        chara.writeValue(value);
    }
protected:
    void save()
    {
        String curKey = currentKey();
        if(value.isEmpty() || value.equals(" ") || value.equals(defaultValue))
        {
            prefs.remove(curKey.c_str());
            value = defaultValue;
        }
        else if(prefs.putString(curKey.c_str(), value) == 0)
        {
            logger.println("failed to store preferences!");
            while (1);
        }
        logger.print(curKey); logger.print(" = "); logger.println(value);
    }
protected:
    virtual String currentKey()
    {
        return key;
    }
    BLEStringCharacteristic chara;
    String key;
    String value;
    const String defaultValue;
    std::function<void(const String&)> applicator;

    BLEDescriptor nameDescriptor;
    BLEDescriptor formatDescriptor;
    BLEDescriptor rangeDescriptor;
};

// Allows a setting to be indexed, so that a single setting can be layered
class StoredMultiProperty : public StoredProperty
{
public:
    StoredMultiProperty(const char *uuid, const char *key, String defaultValue, const char *range, std::function<void(const String&)> applicator) :
        StoredProperty(uuid, key, defaultValue, range, applicator)
    {}

    virtual void load()
    {
        logger.print("Loading every layer's value for key "); logger.println(key);
        int savedLayer = StoredMultiProperty::getLayer();
        // at app launch, load EVERY layer's value
        for(int i = 0; i < LAYER_COUNT; i++)
        {
            StoredMultiProperty::useLayer(i);
            this->StoredProperty::load();
        }
        // and then get the current one's
        StoredMultiProperty::useLayer(savedLayer);
        this->StoredProperty::load();
    }

    static int getLayer() { return currentLayer; }
    static void useLayer(int newLayer) { currentLayer = newLayer; }
protected:
    static int currentLayer;
    virtual String currentKey()
    {
        return key + "-" + String(currentLayer);
    }

    // TODO:
    // 1. Read setting with a key that contains the current layer index √
    // 2. If the current layer index changes, push the new values over bluetooth  √
    // 3. on launch, read all layers and run the applicator for each index so that the app state populates fully √
};
