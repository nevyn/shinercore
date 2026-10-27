#ifndef __WIRING__H
#define __WIRING__H
#include "FastLED.h"
#include <stdint.h>

// Pin roles, the boards they live on, and their wire format. The contract is
// docs/wiring.md. Pure logic, host-tested in test/wiringtest.cpp; the runtime
// that reads the pins is WiringRuntime.h.

#define WIRING_MAX_PINS 8
#define WIRING_MAX_BYTES 512 // the wiring characteristic's size

enum WiringRole : uint8_t { RoleNothing, RoleSwitch, RoleButton, RoleKnob, RoleLeds };
enum SwitchAction : uint8_t { SwitchLightsOff, SwitchLightsOn, SwitchPreset, SwitchLayerOn, SwitchActionCount };
enum ButtonAction : uint8_t { ButtonNextPreset, ButtonToggleLights, ButtonPreset, ButtonToggleLayer, ButtonActionCount };
enum KnobTarget : uint8_t
{
    KnobBrightness, KnobSpeed, KnobTau, KnobPhi,
    KnobColor1R, KnobColor1G, KnobColor1B, KnobColor1H, KnobColor1S, KnobColor1V,
    KnobColor2R, KnobColor2G, KnobColor2B, KnobColor2H, KnobColor2S, KnobColor2V,
    KnobTargetCount
};

// One pin's role. Fields that the role doesn't use stay at their initializers,
// so memberwise equality is equality of meaning.
struct PinRole
{
    WiringRole role = RoleNothing;
    uint8_t action = 0;  // a SwitchAction or ButtonAction, by role
    KnobTarget target = KnobBrightness;
    uint8_t preset = 0;  // for the preset actions
    uint8_t layer = 0;   // for the layer actions and layer targets
    float min = 0, max = 0; // knob range in target units; min > max reverses the knob

    bool operator==(const PinRole &o) const
    {
        return role == o.role && action == o.action && target == o.target &&
               preset == o.preset && layer == o.layer && min == o.min && max == o.max;
    }
    bool operator!=(const PinRole &o) const { return !(*this == o); }
};

inline PinRole nextPresetButton()
{
    PinRole r;
    r.role = RoleButton;
    r.action = ButtonNextPreset;
    return r;
}

// A role per board pin, indexed in the board's status bit order, plus the front button.
struct Wiring
{
    PinRole pins[WIRING_MAX_PINS];
    PinRole frontButton = nextPresetButton(); // always the button role

    bool operator==(const Wiring &o) const
    {
        for(int i = 0; i < WIRING_MAX_PINS; i++) if(pins[i] != o.pins[i]) return false;
        return frontButton == o.frontButton;
    }
    bool operator!=(const Wiring &o) const { return !(*this == o); }
    // Bit i set when pin i has the LED role.
    uint8_t ledPins() const
    {
        uint8_t mask = 0;
        for(int i = 0; i < WIRING_MAX_PINS; i++) if(pins[i].role == RoleLeds) mask |= 1 << i;
        return mask;
    }
};

// A board's user-reachable pins. Every mask has bit i for pin i.
struct BoardSpec
{
    const char *id;
    uint8_t pinCount;
    const char *labels[WIRING_MAX_PINS]; // as printed on the case, in status bit order
    uint8_t gpio[WIRING_MAX_PINS];
    const char *frontButtonLabel; // the button on the case's face; not in the status bit order
    uint8_t analogMask;   // ADC1 pins; ADC2 stops working while wifi (the mesh) is up
    uint8_t ledMask;      // pins that FastLED can drive (it binds them at compile time)
    uint8_t claimMask;    // claimed by built-in hardware, always
    uint8_t micClaimMask; // claimed while the mic setting is on
    const char *claims[WIRING_MAX_PINS]; // what claims each claimable pin, else null

    // Status bit index of the pin with this label or GPIO number, or -1.
    int indexOf(const char *label) const;
    int indexOfGpio(int pin) const;
};

extern const BoardSpec kAtomLite, kAtomEcho, kAtomS3Lite, kStickC;

// The board this firmware is built for, its two Grove signal pins (FastLED
// takes LED pins as template arguments) and its built-in pixel.
#if defined(CONFIG_IDF_TARGET_ESP32S3) // M5AtomS3 Lite
    #define ACTIVE_BOARD kAtomS3Lite
    #define GROVE1_PIN 1
    #define GROVE2_PIN 2
    #define NEO_PIN 35
#elif defined(ARDUINO_M5STACK_ATOM_ECHO) // no core defines this; see docs/building-and-testing.md
    #define ACTIVE_BOARD kAtomEcho
    #define GROVE1_PIN 26
    #define GROVE2_PIN 32
    #define NEO_PIN 27
#elif defined(ARDUINO_M5STACK_ATOM) || defined(ARDUINO_M5Stack_ATOM)
    #define ACTIVE_BOARD kAtomLite
    #define GROVE1_PIN 26
    #define GROVE2_PIN 32
    #define NEO_PIN 27
#elif defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_PLUS) || defined(ARDUINO_M5STACK_STICKC_PLUS)
    #define ACTIVE_BOARD kStickC
    #define GROVE1_PIN 32
    #define GROVE2_PIN 33
    #define NEO_PIN 26 // doesn't have one; this pin is just unused
#else
    #error undefined hardware
#endif

// The wiring that an unstored setting means: the pre-wiring firmware's fixed pins.
Wiring defaultWiring(const BoardSpec &board);

// Parses and validates a wiring write against the board (docs/wiring.md,
// Validation). On rejection, returns false, leaves out unchanged and names the
// failing pin and rule in error.
bool decodeWiring(const BoardSpec &board, const String &json, Wiring &out, String &error);
// The canonical form: pins in status bit order, then the front button, keys in
// contract order, numbers in their shortest round-tripping form.
String encodeWiring(const BoardSpec &board, const Wiring &wiring);
// The wiringDocs characteristic's static JSON: pins, capabilities, claims, targets.
String wiringDocsJSON(const BoardSpec &board);

extern const char *const kKnobTargetNames[KnobTargetCount];
// The target's valid range in its own units, which bounds a knob's min and max.
void knobTargetRange(KnobTarget target, float &lo, float &hi);
// True for targets that address a layer (everything but brightness).
inline bool knobTargetIsLayered(KnobTarget target) { return target != KnobBrightness; }

// The target value for a knob at position 0-1, between min (at 0) and max (at 1).
// Brightness follows a square law; speed, tau and phi are logarithmic when min
// and max are both positive; everything else is linear.
float knobValue(KnobTarget target, float min, float max, float position);

// The pinStatus characteristic: lowercase hex of rev, closed mask, claimed mask,
// then one byte per position, e.g. "07013c9e".
String encodePinStatus(uint8_t revision, uint8_t closedMask, uint8_t claimedMask,
                       const uint8_t *positions, int positionCount);

// Writes one HSV channel (0 h, 1 s, 2 v) of a stored RGB color. It holds the HSV
// value it last wrote, so that RGB round trips don't drift the other two
// channels, and re-derives it only when something else changed the stored color.
struct HsvChannelHold
{
    CHSV hsv;
    CRGB written;
    bool holding = false;

    CRGB write(const CRGB &stored, int channel, uint8_t value)
    {
        if(!holding || stored != written) hsv = rgb2hsv_approximate(stored);
        hsv[channel] = value;
        written = CRGB(hsv);
        holding = true;
        return written;
    }
};

#endif
