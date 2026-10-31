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
    int writeCount = 0; // puts and removes: flash writes, or at least attempts
    String getString(const char *k, const char *def) { auto it = strings.find(k); return it == strings.end() ? String(def) : String(it->second); }
    size_t putString(const char *k, const String &v) { putCount++; writeCount++; strings[k] = v.c_str(); return v.s.size(); }
    void remove(const char *k) { writeCount++; strings.erase(k); }
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
// One frame's worth of persistence for a lone property: its timers advance, then
// everything settled lands
static void settle(Property &p, float dt) { p.update(dt); while(p.flushSettledPersist()); }
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
        settle(f.brightness, 0.3f);
        expect("mid-debounce", nvs("brightness"), "<absent>");
        f.brightness.set(30);        // restarts the settle window
        settle(f.brightness, 0.3f);
        expect("restarted window", nvs("brightness"), "<absent>");
        settle(f.brightness, 0.3f);   // 0.6s since last set
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
        settle(f.brightness, 1.0f);
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
        f.tau.poll();
        expectInt("a key change writes nothing yet", prefs.writeCount, 0);
        expectInt("each key pending on its own", f.tau._pending.size(), 2);
        settle(f.tau, 1.0f);
        expect("old key lands where it was aimed", nvs("tau-0"), "5.500");
        expect("new key settles", nvs("tau-3"), "7.500");
        expectInt("layer 0 field", (long)localPrefs.layers[0].p_tau, 5);
        expectInt("layer 3 field", (long)localPrefs.layers[3].p_tau, 7);
    }

    printf("explicit flushPersist lands the pending write (restart path)\n");
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
        settle(f.brightness, 1.0f);
        f.brightness.chara.incoming = ""; f.brightness.chara._written = true;
        f.brightness.poll();
        settle(f.brightness, 1.0f);
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
        settle(f.tau, 1.0f);
        expect("first key settles", nvs("tau-4-2"), "50.000");
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
        settle(wiring, 1.0f);
        expect("persists canonically", nvs("wiring"), custom);
        wiring.chara.incoming = ""; wiring.chara._written = true;
        wiring.poll();
        settle(wiring, 1.0f);
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
        expectInt("nothing written synchronously", prefs.writeCount, 0);
        settle(f.tau, 1.0f);
        expect("layer 0 holds old layer 1", nvs("tau-0-2"), "2.000");
        expect("layer 1 holds the default as absence", nvs("tau-1-2"), "<absent>");
        expect("layer 2 holds old layer 0", nvs("tau-2-2"), "1.000");
        expect("layers above untouched", nvs("tau-3-2"), "<absent>");
        expectInt("unchanged layers not written", prefs.putCount, 2);
    }

    printf("a ten-layer move persists every field, one NVS write per frame\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        LayerProperty<float> speed{"s", "speed", &ShinyLayerSettings::speed, 0.001f, 100.0f};
        LayerProperty<CRGB> color{"c", "color1", &ShinyLayerSettings::mainColor};
        LayerProperty<CRGB> color2{"c2", "color2", &ShinyLayerSettings::secondaryColor};
        LayerProperty<float> tau{"t", "tau", &ShinyLayerSettings::p_tau, -100.0f, 100.0f};
        LayerProperty<float> phi{"p", "phi", &ShinyLayerSettings::p_phi, -100.0f, 100.0f};
        LayerProperty<LayerBlendMode> blendMode{"b", "blendMode", &ShinyLayerSettings::blendMode};
        LayerProperty<int, AnimationCodec> animation{"a", "animation", &ShinyLayerSettings::animationIndex};
        LayerProperty<int> beatSync{"bs", "beatSync", &ShinyLayerSettings::beatSync, 0, 1};
        std::vector<LayerPropertyBase*> layerProps = {&speed, &color, &color2, &tau, &phi, &animation, &blendMode, &beatSync};
        std::vector<Property*> props(layerProps.begin(), layerProps.end());
        auto pendingCount = [&] { size_t n = 0; for(auto p: props) n += p->_pending.size(); return n; };

        localPrefs.currentPresetIndex = 3;
        ShinyLayerSettings defaults[LAYER_COUNT];
        for(int i = 0; i < LAYER_COUNT; i++) {
            ShinyLayerSettings &l = localPrefs.layers[i];
            l.animationIndex = i + 1;
            l.blendMode = (LayerBlendMode)(i % BlendModeCount);
            l.mainColor = CRGB(i, 2 * i, 3 * i);
            l.secondaryColor = CRGB(255 - i, 128, i);
            l.speed = i + 0.5f;
            l.p_tau = -i - 0.25f;
            l.p_phi = i * 2 + 0.125f;
            l.beatSync = i % 2;
        }
        for(auto p: layerProps) p->persistChanged(defaults);
        for(auto p: props) p->flushPersist();
        prefs.writeCount = 0;

        ShinyLayerSettings before[LAYER_COUNT];
        std::copy(localPrefs.layers, localPrefs.layers + LAYER_COUNT, before);
        moveLayer(localPrefs.layers, 0, 9);
        ShinyLayerSettings moved[LAYER_COUNT];
        std::copy(localPrefs.layers, localPrefs.layers + LAYER_COUNT, moved);
        for(auto p: layerProps) p->persistChanged(before);
        expectInt("every field of every layer pending", pendingCount(), 8 * LAYER_COUNT);
        expectInt("nothing written synchronously", prefs.writeCount, 0);

        size_t next = 0;
        int frames = 0, writingFrames = 0, maxWritesPerFrame = 0;
        while(pendingCount() && frames < 1000) {
            int writesBefore = prefs.writeCount;
            for(auto p: props) p->update(1 / 60.0f);
            flushOneSettledPersist(props, next);
            int writes = prefs.writeCount - writesBefore;
            maxWritesPerFrame = std::max(maxWritesPerFrame, writes);
            writingFrames += writes > 0;
            frames++;
        }
        printf("  at 60 fps: %d frames until every write landed, %d of them writing\n", frames, writingFrames);
        expectInt("at most one NVS write per frame", maxWritesPerFrame, 1);
        expectInt("one write per changed field", prefs.writeCount, 8 * LAYER_COUNT);

        for(int i = 0; i < LAYER_COUNT; i++) localPrefs.layers[i] = ShinyLayerSettings();
        for(auto p: layerProps) for(int i = 0; i < LAYER_COUNT; i++) p->load(i, 3);
        bool same = true;
        for(int i = 0; i < LAYER_COUNT; i++) {
            const ShinyLayerSettings &a = localPrefs.layers[i], &b = moved[i];
            same = same && a.animationIndex == b.animationIndex && a.blendMode == b.blendMode
                && a.mainColor == b.mainColor && a.secondaryColor == b.secondaryColor && a.speed == b.speed
                && a.p_tau == b.p_tau && a.p_phi == b.p_phi && a.beatSync == b.beatSync;
        }
        expectInt("reloading the preset from NVS gives the moved layers", same, 1);
    }

    printf("an edit right after a move persists the final value\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        localPrefs.layers[0].p_tau = 1;
        ShinyLayerSettings before[LAYER_COUNT];
        std::copy(localPrefs.layers, localPrefs.layers + LAYER_COUNT, before);
        moveLayer(localPrefs.layers, 0, 2);
        f.tau.persistChanged(before);
        f.tau.update(0.1f);
        f.tau.setLayer(2, 42.0f);
        f.tau.update(0.45f);         // the move's write would have settled by now
        expectInt("the edit restarts the settle", f.tau.flushSettledPersist() ? 1 : 0, 1);
        expectInt("layer 0 lands, as absence: it holds a default layer now", prefs.writeCount, 1);
        expect("layer 2 not yet", nvs("tau-2"), "<absent>");
        settle(f.tau, 0.1f);
        expect("layer 2 holds the edit", nvs("tau-2"), "42.000");
        settle(f.tau, 1.0f);
        expectInt("one write per key", prefs.writeCount, 2);
    }

    printf("loading a preset reads values that are still pending\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        prefs.strings["tau-1"] = "3.000";
        f.tau.setLayer(1, 5.0f);     // preset 0, unsettled
        localPrefs.layers[1].p_tau = 0;
        f.tau.load(1, 0);            // a quick switch away and back
        expectInt("pending value wins over NVS", (long)localPrefs.layers[1].p_tau, 5);
        expect("storedString sees it", f.tau.storedString("tau-1", "").c_str(), "5.000");
        expect("storedString falls back to NVS", f.tau.storedString("tau-2", "x").c_str(), "x");
    }

    printf("the persist limiter writes one settled key per call, round-robin\n");
    {
        localPrefs = ShinySettings();
        prefs = Prefs();
        Fixture f;
        std::vector<Property*> props = {&f.brightness, &f.tau};
        f.brightness.set(10);
        f.tau.setLayer(0, 1.0f);
        f.tau.setLayer(1, 2.0f);
        size_t next = 0;
        expectInt("nothing settled, nothing written", flushOneSettledPersist(props, next) ? 1 : 0, 0);
        for(auto p: props) p->update(1.0f);
        flushOneSettledPersist(props, next);
        expectInt("first call: one write", prefs.writeCount, 1);
        expect("brightness first", nvs("brightness"), "10");
        flushOneSettledPersist(props, next);
        expect("then tau's oldest", nvs("tau-0"), "1.000");
        flushOneSettledPersist(props, next);
        expect("then tau's next", nvs("tau-1"), "2.000");
        expectInt("then nothing", flushOneSettledPersist(props, next) ? 1 : 0, 0);
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
