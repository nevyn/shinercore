#include "ShinyTypes.h"
#include "Wiring.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const BoardSpec kAtomLite = {
    "atom-lite", 8,
    {"G21", "G25", "G22", "G19", "G23", "G33", "G26", "G32"},
    {21, 25, 22, 19, 23, 33, 26, 32},
    0xA0, 0xC0, 0x00, 0x30,
    {nullptr, nullptr, nullptr, nullptr, "microphone", "microphone", nullptr, nullptr},
};
const BoardSpec kAtomEcho = {
    "atom-echo", 8,
    {"G21", "G25", "G22", "G19", "G23", "G33", "G26", "G32"},
    {21, 25, 22, 19, 23, 33, 26, 32},
    0x80, 0xC0, 0x3C, 0x00,
    {nullptr, nullptr, "speaker", "speaker", "microphone", "microphone", nullptr, nullptr},
};
const BoardSpec kAtomS3Lite = {
    "atoms3-lite", 8,
    {"G39", "G38", "G5", "G6", "G7", "G8", "G2", "G1"},
    {39, 38, 5, 6, 7, 8, 2, 1},
    0xFC, 0xC0, 0x00, 0x00,
    {},
};
// No wiring pins: its two LED outputs stay fixed.
const BoardSpec kStickC = {"m5stickc", 0, {}, {}, 0, 0, 0, 0, {}};

int BoardSpec::indexOf(const char *label) const
{
    for(int i = 0; i < pinCount; i++) if(strcmp(labels[i], label) == 0) return i;
    return -1;
}

int BoardSpec::indexOfGpio(int pin) const
{
    for(int i = 0; i < pinCount; i++) if(gpio[i] == pin) return i;
    return -1;
}

static const char *const kRoleKeys[] = {nullptr, "switch", "button", "knob", "leds"};
static const char *const kSwitchActionNames[SwitchActionCount] = {"lightsOff", "lightsOn", "preset", "layerOn"};
static const char *const kButtonActionNames[ButtonActionCount] = {"nextPreset", "toggleLights", "preset", "toggleLayer"};
const char *const kKnobTargetNames[KnobTargetCount] = {
    "brightness", "speed", "tau", "phi",
    "color1.r", "color1.g", "color1.b", "color1.h", "color1.s", "color1.v",
    "color2.r", "color2.g", "color2.b", "color2.h", "color2.s", "color2.v",
};

void knobTargetRange(KnobTarget target, float &lo, float &hi)
{
    switch(target)
    {
        case KnobSpeed: lo = 0.001f; hi = 100.0f; break;
        case KnobTau: case KnobPhi: lo = -100.0f; hi = 100.0f; break;
        default: lo = 0; hi = 255; break;
    }
}

Wiring defaultWiring(const BoardSpec &board)
{
    // Every board orders its pins so that the pre-wiring layout lands on the same bits
    Wiring w;
    for(int i = 0; i < board.pinCount; i++)
    {
        PinRole &r = w.pins[i];
        if(board.claimMask & (1 << i)) continue;
        if(board.ledMask & (1 << i)) r.role = RoleLeds;
        else if(i == 0) { r.role = RoleButton; r.action = ButtonNextPreset; }
        else if(i == 1) { r.role = RoleSwitch; r.action = SwitchLightsOff; }
        else if(i <= 5) { r.role = RoleSwitch; r.action = SwitchPreset; r.preset = i - 2; }
    }
    return w;
}

// ---- decoding ----------------------------------------------------------------

static bool fail(String &error, const char *pin, const String &why)
{
    error = String(pin) + ": " + why;
    return false;
}

static int lookup(const char *const *names, int count, const char *name)
{
    for(int i = 0; i < count; i++) if(name && strcmp(names[i], name) == 0) return i;
    return -1;
}

static bool readIndex(JsonObjectConst obj, const char *pin, const char *key, int count, uint8_t &out, String &error)
{
    JsonVariantConst v = obj[key];
    if(v.isNull()) return fail(error, pin, String("missing \"") + key + "\"");
    if(!v.is<int>() || v.as<int>() < 0 || v.as<int>() >= count)
        return fail(error, pin, String("\"") + key + "\" must be an integer 0-" + String(count - 1));
    out = v.as<int>();
    return true;
}

static bool readNumber(JsonObjectConst obj, const char *pin, const char *key, float lo, float hi, float &out, String &error)
{
    JsonVariantConst v = obj[key];
    if(v.isNull()) return fail(error, pin, String("missing \"") + key + "\"");
    float f = v.as<float>();
    if(!v.is<float>() || !isfinite(f) || f < lo || f > hi)
        return fail(error, pin, String("\"") + key + "\" must be a number in " + String(lo, 3) + " to " + String(hi, 3));
    out = f;
    return true;
}

static bool decodePinRole(const BoardSpec &board, int i, JsonVariantConst value, PinRole &r, String &error)
{
    const char *pin = board.labels[i];
    if(!value.is<JsonObjectConst>()) return fail(error, pin, "role must be an object");
    JsonObjectConst obj = value.as<JsonObjectConst>();

    int roleKeys = 0;
    for(int role = RoleSwitch; role <= RoleLeds; role++)
    {
        if(!obj[kRoleKeys[role]].isNull()) { r.role = (WiringRole)role; roleKeys++; }
    }
    if(roleKeys != 1) return fail(error, pin, "needs exactly one of \"switch\", \"button\", \"knob\", \"leds\"");

    bool wantsPreset = false, wantsLayer = false, wantsRange = false;
    const char *name = obj[kRoleKeys[r.role]].is<const char *>() ? obj[kRoleKeys[r.role]].as<const char *>() : nullptr;
    switch(r.role)
    {
        case RoleLeds:
            if(!(board.ledMask & (1 << i))) return fail(error, pin, "can't drive LEDs");
            if(!obj["leds"].is<bool>() || !obj["leds"].as<bool>()) return fail(error, pin, "\"leds\" must be true");
            break;
        case RoleSwitch:
        {
            int a = lookup(kSwitchActionNames, SwitchActionCount, name);
            if(a < 0) return fail(error, pin, "unknown switch action");
            r.action = a;
            wantsPreset = a == SwitchPreset;
            wantsLayer = a == SwitchLayerOn;
            break;
        }
        case RoleButton:
        {
            int a = lookup(kButtonActionNames, ButtonActionCount, name);
            if(a < 0) return fail(error, pin, "unknown button action");
            r.action = a;
            wantsPreset = a == ButtonPreset;
            wantsLayer = a == ButtonToggleLayer;
            break;
        }
        case RoleKnob:
        {
            if(!(board.analogMask & (1 << i))) return fail(error, pin, "can't be a knob: not an analog pin");
            int t = lookup(kKnobTargetNames, KnobTargetCount, name);
            if(t < 0) return fail(error, pin, "unknown knob target");
            r.target = (KnobTarget)t;
            wantsLayer = knobTargetIsLayered(r.target);
            wantsRange = true;
            break;
        }
        default: break;
    }

    for(JsonPairConst kv: obj)
    {
        const char *k = kv.key().c_str();
        bool expected = strcmp(k, kRoleKeys[r.role]) == 0 ||
                        (wantsPreset && strcmp(k, "preset") == 0) ||
                        (wantsLayer && strcmp(k, "layer") == 0) ||
                        (wantsRange && (strcmp(k, "min") == 0 || strcmp(k, "max") == 0));
        if(!expected) return fail(error, pin, String("unexpected key \"") + k + "\"");
    }
    if(wantsPreset && !readIndex(obj, pin, "preset", PRESET_COUNT, r.preset, error)) return false;
    if(wantsLayer && !readIndex(obj, pin, "layer", LAYER_COUNT, r.layer, error)) return false;
    if(wantsRange)
    {
        float lo, hi;
        knobTargetRange(r.target, lo, hi);
        if(!readNumber(obj, pin, "min", lo, hi, r.min, error)) return false;
        if(!readNumber(obj, pin, "max", lo, hi, r.max, error)) return false;
    }
    return true;
}

bool decodeWiring(const BoardSpec &board, const String &json, Wiring &out, String &error)
{
    JsonDocument doc;
    DeserializationError parsed = deserializeJson(doc, json.c_str());
    if(parsed) { error = String("not JSON: ") + parsed.c_str(); return false; }
    if(!doc.is<JsonObjectConst>()) { error = "not a JSON object"; return false; }

    Wiring w;
    for(JsonPairConst kv: doc.as<JsonObjectConst>())
    {
        const char *pin = kv.key().c_str();
        int i = board.indexOf(pin);
        if(i < 0) return fail(error, pin, String("not a pin of ") + board.id);
        if(board.claimMask & (1 << i)) return fail(error, pin, String("claimed by the ") + board.claims[i]);
        if(!decodePinRole(board, i, kv.value(), w.pins[i], error)) return false;
    }
    String canonical = encodeWiring(board, w);
    if(canonical.length() > WIRING_MAX_BYTES)
    {
        error = String("canonical form is ") + String((int)canonical.length()) + " bytes, over the " + String(WIRING_MAX_BYTES) + "-byte limit";
        return false;
    }
    out = w;
    return true;
}

// ---- encoding ----------------------------------------------------------------

static String jsonNumber(float v)
{
    char buf[24];
    if(fabsf(v) < 1e7f && v == (float)(long)v)
    {
        snprintf(buf, sizeof buf, "%ld", (long)v);
    }
    else for(int precision = 1; precision <= 9; precision++)
    {
        snprintf(buf, sizeof buf, "%.*g", precision, (double)v);
        if(strtof(buf, nullptr) == v) break;
    }
    return String(buf);
}

static String quoted(const char *s) { return String("\"") + s + "\""; }

static String encodePinRole(const PinRole &r)
{
    String json = "{" + quoted(kRoleKeys[r.role]) + ":";
    switch(r.role)
    {
        case RoleLeds: json += "true"; break;
        case RoleSwitch:
            json += quoted(kSwitchActionNames[r.action]);
            if(r.action == SwitchPreset) json += ",\"preset\":" + String((int)r.preset);
            if(r.action == SwitchLayerOn) json += ",\"layer\":" + String((int)r.layer);
            break;
        case RoleButton:
            json += quoted(kButtonActionNames[r.action]);
            if(r.action == ButtonPreset) json += ",\"preset\":" + String((int)r.preset);
            if(r.action == ButtonToggleLayer) json += ",\"layer\":" + String((int)r.layer);
            break;
        case RoleKnob:
            json += quoted(kKnobTargetNames[r.target]);
            if(knobTargetIsLayered(r.target)) json += ",\"layer\":" + String((int)r.layer);
            json += ",\"min\":" + jsonNumber(r.min) + ",\"max\":" + jsonNumber(r.max);
            break;
        default: break;
    }
    return json + "}";
}

String encodeWiring(const BoardSpec &board, const Wiring &wiring)
{
    String json = "{";
    bool first = true;
    for(int i = 0; i < board.pinCount; i++)
    {
        if(wiring.pins[i].role == RoleNothing) continue;
        if(!first) json += ",";
        first = false;
        json += quoted(board.labels[i]) + ":" + encodePinRole(wiring.pins[i]);
    }
    return json + "}";
}

static String labelList(const BoardSpec &board, uint8_t mask)
{
    String json = "[";
    bool first = true;
    for(int i = 0; i < board.pinCount; i++)
    {
        if(!(mask & (1 << i))) continue;
        if(!first) json += ",";
        first = false;
        json += quoted(board.labels[i]);
    }
    return json + "]";
}

String wiringDocsJSON(const BoardSpec &board)
{
    String json = "{\"board\":" + quoted(board.id);
    json += ",\"pins\":" + labelList(board, 0xFF);
    json += ",\"analog\":" + labelList(board, board.analogMask);
    json += ",\"led\":" + labelList(board, board.ledMask);
    // claims in GPIO order, as docs/wiring.md lists them
    json += ",\"claims\":{";
    bool first = true;
    for(int pin = 0; pin < 64; pin++)
    {
        int i = board.indexOfGpio(pin);
        if(i < 0 || !board.claims[i]) continue;
        if(!first) json += ",";
        first = false;
        json += quoted(board.labels[i]) + ":" + quoted(board.claims[i]);
    }
    json += "},\"targets\":[";
    for(int t = 0; t < KnobTargetCount; t++)
    {
        if(t > 0) json += ",";
        json += quoted(kKnobTargetNames[t]);
    }
    return json + "]}";
}

// ---- knobs and status --------------------------------------------------------

float knobValue(KnobTarget target, float min, float max, float position)
{
    if(target == KnobBrightness) return min + (max - min) * position * position;
    bool logarithmic = (target == KnobSpeed || target == KnobTau || target == KnobPhi) && min > 0 && max > 0;
    if(logarithmic) return min * powf(max / min, position);
    return min + (max - min) * position;
}

String encodePinStatus(uint8_t revision, uint8_t closedMask, uint8_t claimedMask,
                       const uint8_t *positions, int positionCount)
{
    char buf[2 * (3 + WIRING_MAX_PINS) + 1];
    int n = snprintf(buf, sizeof buf, "%02x%02x%02x", revision, closedMask, claimedMask);
    for(int i = 0; i < positionCount && i < WIRING_MAX_PINS; i++)
    {
        n += snprintf(buf + n, sizeof buf - n, "%02x", positions[i]);
    }
    return String(buf);
}
