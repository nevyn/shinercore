// Host-side harness for Migration.h: fakes Arduino String, Preferences and logger,
// then runs migrateSettings() against hand-built NVS contents.
#include <string>
#include <map>
#include <cstdio>
#include <cstdlib>

class String {
public:
    std::string s;
    String() {}
    String(const char *v) : s(v) {}
    String(const std::string &v) : s(v) {}
    String(int i) : s(std::to_string(i)) {}
    bool isEmpty() const { return s.empty(); }
    int toInt() const { return atoi(s.c_str()); }
    const char *c_str() const { return s.c_str(); }
    size_t size() const { return s.size(); }
    operator const std::string&() const { return s; }
    String operator+(const String &o) const { return String(s + o.s); }
    String &operator+=(const String &o) { s += o.s; return *this; }
};

struct Prefs {
    std::map<std::string, std::string> strings;
    std::map<std::string, unsigned> uints;
    String getString(const char *k, const char *def) { auto it = strings.find(k); return it == strings.end() ? String(def) : String(it->second); }
    size_t putString(const char *k, const String &v) { strings[k] = v; return v.size(); }
    void remove(const char *k) { strings.erase(k); }
    unsigned getUInt(const char *k, unsigned def) { auto it = uints.find(k); return it == uints.end() ? def : it->second; }
    size_t putUInt(const char *k, unsigned v) { uints[k] = v; return 4; }
} prefs;

struct Logger {
    void print(const String &s) { printf("%s", s.c_str()); }
    void println(const String &s) { printf("%s\n", s.c_str()); }
} logger;

#define LAYER_COUNT 10
#define PRESET_COUNT 5

// mirrors ShinyTypes.cpp
String layerKey(const String &key, int layer, int preset) {
    String k = key + "-" + String(layer);
    if(preset > 0) { k = k + "-" + String(preset); }
    return k;
}

#include "../Migration.h"

static int failures = 0;
static void expect(const char *what, const std::string &got, const std::string &want) {
    bool ok = got == want;
    if(!ok) failures++;
    printf("  %s %s = '%s' (want '%s')\n", ok ? "ok  " : "FAIL", what, got.c_str(), want.c_str());
}
static std::string val(const char *k) { auto it = prefs.strings.find(k); return it == prefs.strings.end() ? "<absent>" : it->second; }

static void reset() { prefs.strings.clear(); prefs.uints.clear(); }

int main() {
    printf("fresh core\n");
    reset();
    migrateSettings();
    expect("animation-0", val("animation-0"), "Opposing Waves");
    expect("version", std::to_string(prefs.getUInt("settingsVer", 0)), "1");

    printf("fresh core, second boot (must not re-seed after user clears it)\n");
    prefs.strings.erase("animation-0");
    migrateSettings();
    expect("animation-0", val("animation-0"), "<absent>");

    printf("pre-layer core with tweaked colors and mode=2 (Breathe)\n");
    reset();
    prefs.strings = {{"speed","0.8"},{"color1","10 20 30"},{"color2","40 50 60"},{"tau","12.5"},{"phi","3.0"},{"mode","2"},{"brightness","200"},{"name","nevyn"}};
    migrateSettings();
    expect("speed-0", val("speed-0"), "0.8");
    expect("color1-0", val("color1-0"), "10 20 30");
    expect("color2-0", val("color2-0"), "40 50 60");
    expect("tau-0", val("tau-0"), "12.5");
    expect("phi-0", val("phi-0"), "3.0");
    expect("animation-0", val("animation-0"), "Breathe");
    expect("old speed gone", val("speed"), "<absent>");
    expect("mode reset to default", val("mode"), "<absent>");
    expect("brightness kept", val("brightness"), "200");
    expect("name kept", val("name"), "nevyn");

    printf("pre-layer core, default animation, turned off\n");
    reset();
    prefs.strings = {{"color1","1 2 3"},{"mode","0"}};
    migrateSettings();
    expect("color1-0", val("color1-0"), "1 2 3");
    expect("animation-0", val("animation-0"), "Opposing Waves");
    expect("stays off", val("mode"), "0");

    printf("pre-layer core set to Fire (no equivalent today)\n");
    reset();
    prefs.strings = {{"phi","9"},{"mode","3"}};
    migrateSettings();
    expect("animation-0", val("animation-0"), "Opposing Waves");

    printf("pre-layer core with only mode=1 stored\n");
    reset();
    prefs.strings = {{"mode","1"},{"speed","2.0"}};
    migrateSettings();
    expect("animation-0", val("animation-0"), "Opposing Waves");
    expect("mode kept", val("mode"), "1");

    printf("already-upgraded core using layer 3 only, mode=1 stored\n");
    reset();
    prefs.strings = {{"animation-3","Sparkle"},{"color1-3","9 9 9"},{"mode","1"},{"layer","3"}};
    migrateSettings();
    expect("layer 3 untouched", val("animation-3"), "Sparkle");
    expect("no layer 0 seeded", val("animation-0"), "<absent>");
    expect("mode kept", val("mode"), "1");

    printf("already-upgraded core with animations only in preset 2\n");
    reset();
    prefs.strings = {{"animation-1-2","Comet"}};
    migrateSettings();
    expect("no layer 0 seeded", val("animation-0"), "<absent>");

    printf("pre-layer core upgraded to layers before this commit, then configured\n");
    reset();
    prefs.strings = {
        {"speed","0.5"},{"color1","1 1 1"},{"tau","99"},{"mode","2"},        // orphans the layers firmware never read
        {"animation-0","Scanner"},{"color1-0","7 7 7"},{"speed-0","3.0"},    // what the user set up since
        {"animation-2-1","Twinkle"},
    };
    migrateSettings();
    expect("layer 0 animation kept", val("animation-0"), "Scanner");
    expect("layer 0 color kept", val("color1-0"), "7 7 7");
    expect("layer 0 speed kept", val("speed-0"), "3.0");
    expect("preset 1 kept", val("animation-2-1"), "Twinkle");
    expect("orphan speed dropped", val("speed"), "<absent>");
    expect("orphan color1 dropped", val("color1"), "<absent>");
    expect("orphan tau dropped", val("tau"), "<absent>");
    expect("stale mode reset", val("mode"), "<absent>");

    printf("same, but configured only with colors (no animation set anywhere)\n");
    reset();
    prefs.strings = {{"color1","1 1 1"},{"color1-4","7 7 7"}};
    migrateSettings();
    expect("layer 4 color kept", val("color1-4"), "7 7 7");
    expect("orphan dropped", val("color1"), "<absent>");
    expect("no layer 0 written", val("color1-0"), "<absent>");

    printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
