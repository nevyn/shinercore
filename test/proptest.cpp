// Host-side harness for StoredProperty.h's persist/publish state machine: the
// debounce, the only-latest coalescing, and the key-captured-at-set semantics
// that keep a moving layer/preset cursor from misfiling a pending write.
// BLE and Preferences are faked; see docs/ble-comms.md for why this decoupling
// exists at all.
#include "FastLED.h"
#include <map>
#include <vector>

// ---- Arduino/BLE surface fakes ----------------------------------------------
struct Prefs {
    std::map<std::string, std::string> strings;
    int putCount = 0;
    String getString(const char *k, const char *def) { auto it = strings.find(k); return it == strings.end() ? String(def) : String(it->second); }
    size_t putString(const char *k, const String &v) { putCount++; strings[k] = v.c_str(); return v.s.size(); }
    void remove(const char *k) { strings.erase(k); }
} prefs;

struct Logger {
    void print(const String &) {}
    void println(const String &) {}
} logger;

enum { BLERead = 1, BLEWrite = 2, BLENotify = 4 };
class BLEDescriptor {
public:
    BLEDescriptor(const char *, const char *) {}
};
class BLEStringCharacteristic {
public:
    std::string stored;      // the GATT value store
    int notifies = 0;        // writeValue calls = notify attempts
    std::string incoming;    // what a central "wrote"
    bool _written = false;
    BLEStringCharacteristic(const char *, int, int) {}
    void addDescriptor(BLEDescriptor &) {}
    const char *uuid() { return "stub"; }
    bool written() { bool w = _written; _written = false; return w; }
    String value() { return String(incoming); }
    int writeValue(const String &v) { stored = v.c_str(); notifies++; return 1; }
};
class BLEService {
public:
    void addCharacteristic(BLEStringCharacteristic &) {}
};

#include "../ShinyTypes.h"
ShinySettings localPrefs;

#define protected public // the harness pokes at chara and the staging state
#include "../StoredProperty.h"
#undef protected

// ---- harness ----------------------------------------------------------------
static int failures = 0;
static void expect(const char *what, const std::string &got, const std::string &want) {
    bool ok = got == want;
    if(!ok) failures++;
    printf("  %s %s = '%s' (want '%s')\n", ok ? "ok  " : "FAIL", what, got.c_str(), want.c_str());
}
static void expectInt(const char *what, long got, long want) {
    expect(what, std::to_string(got), std::to_string(want));
}
static std::string nvs(const char *k) { auto it = prefs.strings.find(k); return it == prefs.strings.end() ? "<absent>" : it->second; }

// Test doubles of the sketch's property globals
struct Fixture {
    GlobalProperty<int> brightness{"u1", "brightness", &localPrefs.brightness, 0, 255};
    LayerProperty<float> tau{"u2", "tau", &ShinyLayerSettings::p_tau, -100.0f, 100.0f};
};

int main() {
    printf("publish stages only-latest; nothing sent until flushed\n");
    {
        localPrefs = ShinySettings();
        Fixture f;
        f.brightness.set(10);
        f.brightness.set(20);
        f.brightness.set(30);
        expectInt("notifies before flush", f.brightness.chara.notifies, 0);
        expectInt("flush sends", f.brightness.flushPublish() ? 1 : 0, 1);
        expect("latest value only", f.brightness.chara.stored, "30");
        expectInt("one notify total", f.brightness.chara.notifies, 1);
        expectInt("second flush is a no-op", f.brightness.flushPublish() ? 1 : 0, 0);
    }

    printf("persist debounces to one flash write per settle\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        f.brightness.set(10);
        f.brightness.set(20);
        f.brightness.update(0.3f);
        expect("mid-debounce", nvs("brightness"), "<absent>");
        f.brightness.set(30);        // restarts the settle window
        f.brightness.update(0.3f);
        expect("restarted window", nvs("brightness"), "<absent>");
        f.brightness.update(0.3f);   // 0.6s since last set
        expect("settled", nvs("brightness"), "30");
        expectInt("single flash write", prefs.putCount, 1);
    }

    printf("value at default settles as absence\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        prefs.strings["brightness"] = "30";
        Fixture f;
        f.brightness.set(255);       // 255 is the struct default
        f.brightness.update(1.0f);
        expect("default removed from nvs", nvs("brightness"), "<absent>");
    }

    printf("cursor move mid-debounce can't misfile a pending write\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        localPrefs.currentLayerIndex = 0;
        f.tau.chara.incoming = "5.5"; f.tau.chara._written = true;
        f.tau.poll();
        localPrefs.currentLayerIndex = 3;              // cursor moves before settle
        f.tau.chara.incoming = "7.5"; f.tau.chara._written = true;
        f.tau.poll();                                  // must flush layer 0 first
        expect("old key flushed on key change", nvs("tau-0"), "5.500");
        expect("new key still pending", nvs("tau-3"), "<absent>");
        f.tau.update(1.0f);
        expect("new key settles", nvs("tau-3"), "7.500");
        expectInt("layer 0 field", (long)localPrefs.layers[0].p_tau, 5);
        expectInt("layer 3 field", (long)localPrefs.layers[3].p_tau, 7);
    }

    printf("explicit flushPersist lands the pending write (loadLayers path)\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        f.tau.chara.incoming = "9.0"; f.tau.chara._written = true;
        f.tau.poll();
        expect("still pending", nvs("tau-0"), "<absent>");
        f.tau.flushPersist();
        expect("flushed on demand", nvs("tau-0"), "9.000");
        f.tau.flushPersist();        // idempotent
        expectInt("no extra write", prefs.putCount, 1);
    }

    printf("rejected write keeps the field and re-stages the echo\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        f.brightness.set(40);
        f.brightness.flushPublish();
        int notifiesBefore = f.brightness.chara.notifies;
        f.brightness.chara.incoming = "garbage"; f.brightness.chara._written = true;
        f.brightness.poll();
        expectInt("field unchanged", localPrefs.brightness, 40);
        expectInt("echo staged not sent", f.brightness.chara.notifies, notifiesBefore);
        f.brightness.flushPublish();
        expect("echo corrects the store", f.brightness.chara.stored, "40");
    }

    printf("empty write resets to the default\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        f.brightness.set(40);
        f.brightness.update(1.0f);
        f.brightness.chara.incoming = ""; f.brightness.chara._written = true;
        f.brightness.poll();
        f.brightness.update(1.0f);
        expectInt("field back at default", localPrefs.brightness, 255);
        expect("nvs cleared", nvs("brightness"), "<absent>");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
