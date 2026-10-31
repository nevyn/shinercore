// Host-side harness for StoredProperty.h's persist/publish state machine: the
// debounce, the only-latest coalescing, and the key-captured-at-set semantics
// that keep a moving layer/preset cursor from misfiling a pending write.
// BLE and Preferences are faked; see docs/ble-comms.md for why this decoupling
// exists at all.
#include "FastLED.h"
#include <algorithm>
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

    printf("setLayer writes any layer of the current preset\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        localPrefs.currentLayerIndex = 1;
        localPrefs.currentPresetIndex = 2;
        f.tau.setLayer(4, 50.0f);
        expectInt("field set", (long)localPrefs.layers[4].p_tau, 50);
        expectInt("off-cursor layer not republished", f.tau.flushPublish() ? 1 : 0, 0);
        f.tau.setLayer(1, 500.0f);
        expect("cursor layer republished, clamped", f.tau.flushPublish() ? f.tau._publishValue.c_str() : "", "100.000");
        f.tau.update(1.0f);
        expect("first key flushed on key change", nvs("tau-4-2"), "50.000");
        expect("second key settles", nvs("tau-1-2"), "100.000");
    }

    printf("wiring is stored synchronously and every write bumps the revision\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        WiringProperty wiring("u3", "wiring", &localPrefs.wiring);
        const std::string custom = "{\"G21\":{\"button\":\"toggleLights\"},\"G39\":{\"button\":\"nextPreset\"}}";
        wiring.load();
        expectInt("load bumps", wiring.revision, 1);
        expect("default stored at load", wiring.chara.stored, encodeWiring(kAtomLite, defaultWiring(kAtomLite)).c_str());
        wiring.chara.incoming = custom; wiring.chara._written = true;
        wiring.poll();
        expect("canonical stored without a flush", wiring.chara.stored, custom);
        expectInt("applied write bumps", wiring.revision, 2);
        expectInt("nothing staged for the limiter", wiring.flushPublish() ? 1 : 0, 0);
        wiring.chara.incoming = "{\"G99\":{}}"; wiring.chara._written = true;
        wiring.poll();
        expectInt("rejected write bumps", wiring.revision, 3);
        expect("rejected write keeps the wiring", wiring.chara.stored, custom);
        wiring.update(1.0f);
        expect("persists canonically", nvs("wiring"), custom);
        wiring.chara.incoming = ""; wiring.chara._written = true;
        wiring.poll();
        wiring.update(1.0f);
        expectInt("reset bumps", wiring.revision, 4);
        expect("default stored as absence", nvs("wiring"), "<absent>");
        prefs.strings["wiring"] = "{\"G19\":{\"knob\":\"brightness\",\"min\":0,\"max\":255}}";
        wiring.load();
        expect("invalid stored wiring loads as the default", wiring.chara.stored, encodeWiring(kAtomLite, defaultWiring(kAtomLite)).c_str());
    }

    printf("pinStatus stages on change only, at most every 100 ms\n");
    {
        PinStatusProperty status("u4", "pinStatus");
        status.offer("07013c9e", 0.016f);
        expectInt("first offer staged", status.flushPublish() ? 1 : 0, 1);
        status.offer("07013c9f", 0.016f);
        expectInt("change too soon not staged", status.flushPublish() ? 1 : 0, 0);
        status.offer("07013c9f", 0.1f);
        expectInt("change staged once the interval passed", status.flushPublish() ? 1 : 0, 1);
        expect("latest sent", status.chara.stored, "07013c9f");
        status.offer("07013c9f", 0.2f);
        expectInt("unchanged not staged", status.flushPublish() ? 1 : 0, 0);
    }

    printf("layer summary stores on change only, at most every 100 ms, and bumps the revision\n");
    {
        LayerSummaryProperty summary("u5", "u6");
        ShinyLayerSettings layers[LAYER_COUNT];
        const ShinyLayerSettings *slots[LAYER_COUNT];
        bool gates[LAYER_COUNT];
        for(int i = 0; i < LAYER_COUNT; i++) { slots[i] = &layers[i]; gates[i] = true; }
        layers[0].animationIndex = 1;

        summary.offer(0, slots, gates, 0.016f);
        expect("first offer stores", summary.summary.stored, "P 0\n0 1 0 ff6400 f0ff00 1 10 4 0 1");
        expectInt("first offer staged", summary.flushPublish() ? 1 : 0, 1);
        expect("revision 1", summary.chara.stored, "1");

        gates[0] = false;
        summary.offer(0, slots, gates, 0.016f);
        expect("change too soon not stored", summary.summary.stored, "P 0\n0 1 0 ff6400 f0ff00 1 10 4 0 1");
        expectInt("change too soon not staged", summary.flushPublish() ? 1 : 0, 0);
        summary.offer(0, slots, gates, 0.1f);
        expect("gate change stored once the interval passed", summary.summary.stored, "P 0\n0 1 0 ff6400 f0ff00 1 10 4 0 0");
        summary.offer(kLayerSummaryMesh, slots, gates, 0.1f);
        expectInt("only the settled revision is sent", summary.flushPublish() ? 1 : 0, 1);
        expect("revision 3", summary.chara.stored, "3");
        expectInt("one notify per staged revision", summary.chara.notifies, 2);

        summary.offer(kLayerSummaryMesh, slots, gates, 0.2f);
        expectInt("unchanged not staged", summary.flushPublish() ? 1 : 0, 0);
        expectInt("unchanged keeps the revision", summary.revision, 3);

        summary.revision = 65535;
        gates[0] = true;
        summary.offer(kLayerSummaryMesh, slots, gates, 0.1f);
        summary.flushPublish();
        expect("revision wraps", summary.chara.stored, "0");
    }

    printf("a layer move persists the changed layers of the current preset\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        localPrefs.currentPresetIndex = 2;
        localPrefs.layers[0].p_tau = 1;
        localPrefs.layers[1].p_tau = 2;
        prefs.strings["tau-0-2"] = "1.000";
        prefs.strings["tau-1-2"] = "2.000";
        ShinyLayerSettings before[LAYER_COUNT];
        std::copy(localPrefs.layers, localPrefs.layers + LAYER_COUNT, before);
        moveLayer(localPrefs.layers, 0, 2);
        f.tau.persistChanged(before);
        f.tau.update(1.0f);
        expect("layer 0 holds old layer 1", nvs("tau-0-2"), "2.000");
        expect("layer 1 holds the default as absence", nvs("tau-1-2"), "<absent>");
        expect("layer 2 holds old layer 0", nvs("tau-2-2"), "1.000");
        expect("layers above untouched", nvs("tau-3-2"), "<absent>");
        expectInt("unchanged layers not written", prefs.putCount, 2);
    }

    printf("layerMove hands valid moves on and drops the rest\n");
    {
        static int movedFrom, movedTo, moves;
        moves = 0;
        LayerMoveProperty move("u7", "layerMove", [](int from, int to) { movedFrom = from; movedTo = to; moves++; });
        move.chara.incoming = "3 7"; move.chara._written = true;
        move.poll();
        expectInt("valid move handed on", moves, 1);
        expectInt("from", movedFrom, 3);
        expectInt("to", movedTo, 7);
        move.chara.incoming = "3 10"; move.chara._written = true;
        move.poll();
        move.chara.incoming = "garbage"; move.chara._written = true;
        move.poll();
        expectInt("invalid moves dropped", moves, 1);
        expectInt("nothing echoed", move.flushPublish() ? 1 : 0, 0);
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
