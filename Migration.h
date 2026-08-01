#ifndef __MIGRATION__H
#define __MIGRATION__H

// One-shot NVS maintenance, run before any property is loaded.
//
// Layout versions:
//   0: pre-layer cores. One unsuffixed set of keys for the whole strip, and
//      "mode" doubled as the animation selector (1 = DoubleCrawl, 2 = Breathe,
//      3 = Fire).
//   1: one set of keys per layer, with an extra suffix for presets above 0.
#define SETTINGS_VERSION 1
// NVS keys are capped at 15 characters
#define kSettingsVersionKey "settingsVer"

// Where a pre-layer core's single animation lands, and what a core with nothing
// configured anywhere gets seeded with so it never boots black.
#define INITIAL_LAYER 0
#define INITIAL_ANIMATION "Opposing Waves"

// Per-layer settings. The first LEGACY_LAYER_KEY_COUNT of them were per-strip
// in layout 0 and stored unsuffixed; the rest only exist in layout 1.
static const char *kLayerKeys[] = {"speed", "color1", "color2", "tau", "phi", "animation", "blendMode"};
#define LEGACY_LAYER_KEY_COUNT 5

// Layout 0 RunMode 1-3 as animation names. Fire has no equivalent today.
static const char *kLegacyModeAnimations[] = {"Opposing Waves", "Breathe", NULL};

static bool hasValue(const String &key)
{
    // save() removes a key rather than storing an empty value, so "" means absent
    return !prefs.getString(key.c_str(), "").isEmpty();
}

static void storeInitialAnimation(const char *animation)
{
    prefs.putString(layerKey("animation", INITIAL_LAYER, 0).c_str(), animation);
    logger.print("Initial animation := "); logger.println(animation);
}

static bool anyLegacyLayerKey()
{
    for(int i = 0; i < LEGACY_LAYER_KEY_COUNT; i++)
    {
        if(hasValue(kLayerKeys[i])) return true;
    }
    return false;
}

// True if any layer of any preset holds a setting, i.e. this core has already
// been configured under layout 1.
static bool anyLayerConfigured()
{
    for(const char *key: kLayerKeys)
    {
        for(int preset = 0; preset < PRESET_COUNT; preset++)
        {
            for(int layer = 0; layer < LAYER_COUNT; layer++)
            {
                if(hasValue(layerKey(key, layer, preset))) return true;
            }
        }
    }
    return false;
}

static void moveLegacyLayerKeys()
{
    for(int i = 0; i < LEGACY_LAYER_KEY_COUNT; i++)
    {
        const char *key = kLayerKeys[i];
        String value = prefs.getString(key, "");
        if(value.isEmpty()) continue;

        String newKey = layerKey(key, INITIAL_LAYER, 0);
        prefs.putString(newKey.c_str(), value);
        prefs.remove(key);
        logger.print("Migrated "); logger.print(key); logger.print(" -> "); logger.print(newKey);
        logger.print(" = "); logger.println(value);
    }
}

static void removeLegacyLayerKeys()
{
    for(int i = 0; i < LEGACY_LAYER_KEY_COUNT; i++)
    {
        prefs.remove(kLayerKeys[i]);
    }
}

static const char *animationForLegacyMode(int legacyMode)
{
    const char *animation = (legacyMode >= 1 && legacyMode <= 3)
        ? kLegacyModeAnimations[legacyMode - 1]
        : INITIAL_ANIMATION;
    if(!animation)
    {
        logger.println("Fire animation no longer exists; falling back");
        animation = INITIAL_ANIMATION;
    }
    return animation;
}

void migrateSettings()
{
    if(prefs.getUInt(kSettingsVersionKey, 0) >= SETTINGS_VERSION) return;

    int legacyMode = prefs.getString("mode", "").toInt();
    // mode 0 and 1 mean the same thing in both layouts, so only a higher value
    // identifies layout 0 on its own.
    bool legacy = anyLegacyLayerKey() || legacyMode > 1;
    bool configured = anyLayerConfigured();

    if(legacy && configured)
    {
        // Upgraded to layers before this migration existed: the layered firmware
        // never read the unsuffixed keys, and the core has been set up since.
        // Drop them rather than move them over settings that are actually in use.
        logger.println("Discarding pre-layer settings; core was configured after upgrading");
        removeLegacyLayerKeys();
    }
    else if(legacy)
    {
        moveLegacyLayerKeys();
        storeInitialAnimation(animationForLegacyMode(legacyMode));
    }
    else if(!configured)
    {
        // Nothing anywhere, so this core would light up black
        storeInitialAnimation(INITIAL_ANIMATION);
    }

    // An animation index left in mode is stale; On is the default now
    if(legacyMode > 1) prefs.remove("mode");

    prefs.putUInt(kSettingsVersionKey, SETTINGS_VERSION);
}

#endif
