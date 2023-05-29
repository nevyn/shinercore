
#define kDescriptorUserDesc "2901"
#define kDescriptorClientCharacteristic "2902"
#define kDescriptorServerCharacteristic "2903"
#define kDescriptorPresentationFormat "2904"
#define kDescriptorPresentationFormat_S32 "10"
#define kDescriptorPresentationFormat_Float32 "14"
#define kDescriptorPresentationFormat_String "19"
#define kDescriptorValidRange "2906"

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
