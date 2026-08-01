
#define kDescriptorUserDesc "2901"
#define kDescriptorPresentationFormat "2904"
#define kDescriptorPresentationFormat_String "19"

#include "Codecs.h"

// A setting, exposed over bluetooth as a string characteristic and persisted in
// NVS as a string, but decoded into a typed field that business logic reads
// directly. The typed field is the single source of truth; NVS and BLE are
// projections of it. A write that doesn't decode is ignored and the current
// value republished; an empty write resets to the default, which is whatever
// the field held at construction (i.e. the struct initializer).
class Property
{
public:
    Property(const char *uuid, const char *key)
      : chara(uuid, BLERead | BLEWrite, 36),
        key(key),
        nameDescriptor(kDescriptorUserDesc, key),
        formatDescriptor(kDescriptorPresentationFormat, kDescriptorPresentationFormat_String)
    {
        chara.addDescriptor(nameDescriptor);
        chara.addDescriptor(formatDescriptor);
    }
    const char *uuid() { return chara.uuid(); }
    void advertise(BLEService &onService) { onService.addCharacteristic(chara); }
    void poll()
    {
        if(chara.written())
        {
            handleWrite(chara.value());
        }
    }
protected:
    virtual void handleWrite(const String &wire) = 0;
    void publish(const String &encoded) { chara.writeValue(encoded); }
    void persist(const String &curKey, const String &encoded, const String &encodedDefault)
    {
        // A value at its default is stored as absence, which is what lets
        // Migration.h and presetHasAnimations() treat "" as unconfigured
        if(encoded == encodedDefault)
        {
            prefs.remove(curKey.c_str());
        }
        else if(prefs.putString(curKey.c_str(), encoded) == 0)
        {
            logger.println("failed to store preferences!"); // value is still live in RAM; carry on
        }
        logger.print(curKey); logger.print(" = "); logger.println(encoded);
    }
    BLEStringCharacteristic chara;
    String key;
    BLEDescriptor nameDescriptor;
    BLEDescriptor formatDescriptor;
};

class GlobalPropertyBase : public Property
{
public:
    using Property::Property;
    virtual void load() = 0;
};

// A global setting: a typed pointer to the one live copy of the value,
// usually a ShinySettings field.
template<typename T, typename C = Codec<T>>
class GlobalProperty : public GlobalPropertyBase
{
public:
    GlobalProperty(const char *uuid, const char *key, T *field, void (*onChanged)() = nullptr)
      : GlobalPropertyBase(uuid, key), field(field), defaultValue(*field),
        lo(*field), hi(*field), hasRange(false), onChanged(onChanged)
    {}
    GlobalProperty(const char *uuid, const char *key, T *field, T lo, T hi, void (*onChanged)() = nullptr)
      : GlobalPropertyBase(uuid, key), field(field), defaultValue(*field),
        lo(lo), hi(hi), hasRange(true), onChanged(onChanged)
    {}

    const T &get() { return *field; }
    void set(const T &newValue)
    {
        *field = constrained(newValue);
        persist(key, C::encode(*field), C::encode(defaultValue));
        publish(C::encode(*field));
        if(onChanged) onChanged();
    }
    virtual void load()
    {
        String stored = prefs.getString(key.c_str(), "");
        T v;
        *field = (!stored.isEmpty() && C::decode(stored, v)) ? constrained(v) : defaultValue;
        publish(C::encode(*field));
        logger.print(key); logger.print(" := "); logger.println(C::encode(*field));
        if(onChanged) onChanged();
    }
protected:
    virtual void handleWrite(const String &wire)
    {
        T v;
        if(wire.isEmpty() || wire.equals(" "))
        {
            set(defaultValue);
        }
        else if(C::decode(wire, v))
        {
            set(v);
        }
        else
        {
            publish(C::encode(*field)); // rejected; re-echo the actual value
        }
    }
    T constrained(const T &v) { return hasRange ? clampTo(v, lo, hi) : v; }

    T *field;
    const T defaultValue;
    T lo, hi;
    bool hasRange;
    void (*onChanged)();
};

class LayerPropertyBase : public Property
{
public:
    using Property::Property;
    virtual void load(int layer, int preset) = 0;
    virtual void republish() = 0;
};

// A per-layer setting: a field of ShinyLayerSettings, stored per (layer,
// preset) in NVS. All of the current preset's layers live decoded in
// localPrefs.layers; the single characteristic reads and writes the layer
// that localPrefs.currentLayerIndex selects.
template<typename T, typename C = Codec<T>>
class LayerProperty : public LayerPropertyBase
{
public:
    LayerProperty(const char *uuid, const char *key, T ShinyLayerSettings::*member)
      : LayerPropertyBase(uuid, key), member(member), defaultValue(ShinyLayerSettings().*member),
        lo(defaultValue), hi(defaultValue), hasRange(false)
    {}
    LayerProperty(const char *uuid, const char *key, T ShinyLayerSettings::*member, T lo, T hi)
      : LayerPropertyBase(uuid, key), member(member), defaultValue(ShinyLayerSettings().*member),
        lo(lo), hi(hi), hasRange(true)
    {}

    virtual void load(int layer, int preset)
    {
        String stored = prefs.getString(layerKey(key, layer, preset).c_str(), "");
        T v;
        localPrefs.layers[layer].*member = (!stored.isEmpty() && C::decode(stored, v)) ? constrained(v) : defaultValue;
    }
    virtual void republish()
    {
        publish(C::encode(current()));
    }
protected:
    T &current() { return localPrefs.layers[localPrefs.currentLayerIndex].*member; }
    virtual void handleWrite(const String &wire)
    {
        T v;
        if(wire.isEmpty() || wire.equals(" "))
        {
            v = defaultValue;
        }
        else if(!C::decode(wire, v))
        {
            republish(); // rejected; re-echo the actual value
            return;
        }
        current() = constrained(v);
        persist(layerKey(key, localPrefs.currentLayerIndex, localPrefs.currentPresetIndex),
                C::encode(current()), C::encode(defaultValue));
        republish();
    }
    T constrained(const T &v) { return hasRange ? clampTo(v, lo, hi) : v; }

    T ShinyLayerSettings::*member;
    const T defaultValue;
    T lo, hi;
    bool hasRange;
};
