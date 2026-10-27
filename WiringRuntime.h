#ifndef __WIRING_RUNTIME__H
#define __WIRING_RUNTIME__H
// Runs the wiring: registers LED outputs at boot, then reads the pins every frame
// and acts on their roles (docs/wiring.md, Runtime semantics). Writes settings
// through the properties, so the sketch includes it after Comms.h.

static const float kDebounceSeconds = 0.03;
static const float kLongPressSeconds = 0.6;    // a nextPreset button held this long toggles the lights
static const float kLedRestartDelaySeconds = 0.3; // lets the wiring write's response reach the app first
static const float kKnobFilterSeconds = 0.05;  // low-pass time constant on the ADC reading
static const float kKnobEndSnap = 0.01;        // the outer 1% of travel at each end reads as that end
static const float kKnobDeadband = 0.006;      // of travel, from the last written position
static const float kKnobSettleSeconds = 0.5;   // after boot or a role change: a knob only edits when turned

void cycleToNextPreset();
void toggleMode();

// What the runtime has configured a pin as, so it reconfigures only on change
enum PinDrive : uint8_t { DriveNone, DriveUntouched, DrivePullup, DriveAnalog };

struct PinRuntime
{
    PinRole role;
    PinDrive drive = DriveNone;
    bool seeded = false;
    bool raw = false, closed = false; // closed means pulled to G, debounced
    float rawFor = 0;
    float heldFor = 0;
    bool longPressFired = false;
    float filtered = 0, lastWritten = 0, settleLeft = 0; // knob positions are 0-1
    uint8_t reported = 0;
    HsvChannelHold hsv;
};
PinRuntime pinRuntime[WIRING_MAX_PINS];
PinRuntime frontButtonRuntime;
bool layerGate[LAYER_COUNT]; // runtime-only: never stored or sent; opened in wiringSetup
uint8_t registeredLedPins = 0; // FastLED owns these until the core restarts
int stripOutputs = 0;
float ledRestartIn = -1;

// A GPIO outside the board's wiring pins is a fixed LED output (M5StickC)
static bool wiringDrivesLeds(int gpio)
{
    int i = ACTIVE_BOARD.indexOfGpio(gpio);
    return i < 0 || localPrefs.wiring.pins[i].role == RoleLeds;
}

// Registers a strip output for each pin with the LED role. Needs the loaded wiring.
void wiringSetup()
{
    std::fill(layerGate, layerGate + LAYER_COUNT, true);
    if(wiringDrivesLeds(GROVE1_PIN)) { FastLED.addLeds<WS2811, GROVE1_PIN, RGB>(rgbs, MAX_LED_COUNT); stripOutputs++; }
    if(wiringDrivesLeds(GROVE2_PIN)) { FastLED.addLeds<WS2811, GROVE2_PIN, RGB>(rgbs, MAX_LED_COUNT); stripOutputs++; }
    registeredLedPins = localPrefs.wiring.ledPins();
}

// True when the debounced state changed this frame
static bool debounce(PinRuntime &p, bool raw, float dt)
{
    if(!p.seeded)
    {
        // No edge on the first reading: a button that's already held does nothing until pressed again
        p.raw = p.closed = p.longPressFired = raw;
        p.seeded = true;
        return false;
    }
    if(raw != p.raw)
    {
        p.raw = raw;
        p.rawFor = 0;
        return false;
    }
    p.rawFor += dt;
    if(p.closed == raw || p.rawFor < kDebounceSeconds) return false;
    p.closed = raw;
    return true;
}

static void pressButton(const PinRole &role)
{
    switch(role.action)
    {
        case ButtonToggleLights: toggleMode(); break;
        case ButtonPreset: presetProp.set(role.preset); break;
        case ButtonToggleLayer: layerGate[role.layer] = !layerGate[role.layer]; break;
        default: break;
    }
}

// Acts on a debounced button: nextPreset on release or hold, everything else on press
static void runButton(PinRuntime &p, const PinRole &role, bool changed, float dt)
{
    if(role.action != ButtonNextPreset)
    {
        if(changed && p.closed) pressButton(role);
        return;
    }
    if(changed && p.closed)
    {
        p.heldFor = 0;
        p.longPressFired = false;
    }
    else if(changed && !p.longPressFired) cycleToNextPreset();
    if(p.closed && !p.longPressFired && (p.heldFor += dt) >= kLongPressSeconds)
    {
        p.longPressFired = true;
        toggleMode();
    }
}

static void writeLayerFloat(LayerProperty<float> &prop, float current, int layer, float value)
{
    if(Codec<float>::encode(value) != Codec<float>::encode(current)) prop.setLayer(layer, value);
}

// Writes the knob's target through its property, only if the stored value would change
static void writeKnob(PinRuntime &p, const PinRole &role, float position)
{
    float value = knobValue(role.target, role.min, role.max, position);
    ShinyLayerSettings &layer = localPrefs.layers[role.layer];
    switch(role.target)
    {
        case KnobBrightness:
        {
            int brightness = lroundf(value);
            if(brightness != localPrefs.brightness) brightnessProp.set(brightness);
            return;
        }
        case KnobSpeed: writeLayerFloat(speedProp, layer.speed, role.layer, value); return;
        case KnobTau: writeLayerFloat(tauProp, layer.p_tau, role.layer, value); return;
        case KnobPhi: writeLayerFloat(phiProp, layer.p_phi, role.layer, value); return;
        default: break;
    }
    bool secondary = role.target >= KnobColor2R;
    int channel = (role.target - KnobColor1R) % 6; // r g b h s v
    const CRGB stored = secondary ? layer.secondaryColor : layer.mainColor;
    uint8_t v = lroundf(value);
    CRGB next = stored;
    if(channel < 3) next[channel] = v;
    else next = p.hsv.write(stored, channel - 3, v);
    if(next != stored) (secondary ? color2Prop : colorProp).setLayer(role.layer, next);
}

// The knob's filtered position, 0-1, written to its target when someone turns it
static float readKnob(PinRuntime &p, const PinRole &role, int gpio, float dt)
{
    float raw = analogRead(gpio) / 4095.0f;
    if(!p.seeded)
    {
        p.filtered = raw;
        p.settleLeft = kKnobSettleSeconds;
        p.seeded = true;
    }
    else p.filtered += (raw - p.filtered) * (1 - expf(-dt / kKnobFilterSeconds));
    float position = (p.filtered - kKnobEndSnap) / (1 - 2 * kKnobEndSnap);
    position = position < 0 ? 0 : position > 1 ? 1 : position;

    bool atEnd = position == 0 || position == 1;
    if(p.settleLeft > 0)
    {
        p.settleLeft -= dt;
        p.lastWritten = position;
    }
    else if(fabsf(position - p.lastWritten) > kKnobDeadband || (atEnd && position != p.lastWritten))
    {
        p.lastWritten = position;
        writeKnob(p, role, position);
    }
    return position;
}

static void restartForLedPins(float dt)
{
    if(localPrefs.wiring.ledPins() == registeredLedPins)
    {
        ledRestartIn = -1;
        return;
    }
    if(ledRestartIn < 0) ledRestartIn = kLedRestartDelaySeconds;
    if((ledRestartIn -= dt) > 0) return;
    logger.println("wiring changed which pins are LEDs; restarting to register them with FastLED");
    for(const auto &prop: props) prop->flushPersist();
    ESP.restart();
}

void wiringUpdate(float dt)
{
    const BoardSpec &board = ACTIVE_BOARD;
    uint8_t closedMask = 0, claimedMask = 0;
    uint8_t positions[WIRING_MAX_PINS];
    int positionCount = 0;
    bool holdOff = false, holdOn = false;
    int holdPreset = -1;
    int modeOnOpen = -1;
    uint16_t switchedLayers = 0, switchedOnLayers = 0; // a switch overrides button toggles of its layer

    PinRuntime &front = frontButtonRuntime;
    if(front.role != localPrefs.wiring.frontButton)
    {
        front = PinRuntime();
        front.role = localPrefs.wiring.frontButton;
    }
    runButton(front, front.role, debounce(front, M5.BtnA.isPressed(), dt), dt);

    for(int i = 0; i < board.pinCount; i++)
    {
        const PinRole &role = localPrefs.wiring.pins[i];
        PinRuntime &p = pinRuntime[i];
        uint8_t bit = 1 << i;
        bool claimed = (board.claimMask & bit) || ((board.micClaimMask & bit) && localPrefs.micEnabled);
        bool leds = (registeredLedPins & bit) || role.role == RoleLeds;
        PinDrive drive = (claimed || leds) ? DriveUntouched : role.role == RoleKnob ? DriveAnalog : DrivePullup;
        if(claimed) claimedMask |= bit;

        if(drive != p.drive || role != p.role)
        {
            // A new role, or a claim that ended, starts from rest
            p = PinRuntime();
            p.role = role;
            p.drive = drive;
            if(drive == DrivePullup) pinMode(board.gpio[i], INPUT_PULLUP);
            if(drive == DriveAnalog) pinMode(board.gpio[i], ANALOG);
        }

        uint8_t position = 0;
        if(drive == DrivePullup)
        {
            bool changed = debounce(p, digitalRead(board.gpio[i]) == LOW, dt);
            if(p.closed) closedMask |= bit;
            position = p.closed ? 0x00 : 0xff;

            if(role.role == RoleSwitch)
            {
                switch(role.action)
                {
                    case SwitchLightsOff: if(p.closed) holdOff = true; else if(changed) modeOnOpen = On; break;
                    case SwitchLightsOn: if(p.closed) holdOn = true; else if(changed) modeOnOpen = Off; break;
                    case SwitchPreset: if(p.closed && holdPreset < 0) holdPreset = role.preset; break;
                    case SwitchLayerOn:
                        switchedLayers |= 1 << role.layer;
                        if(p.closed) switchedOnLayers |= 1 << role.layer;
                        break;
                    default: break;
                }
            }
            else if(role.role == RoleButton) runButton(p, role, changed, dt);
        }
        else if(drive == DriveAnalog)
        {
            // Hysteresis keeps ADC noise from flipping the reported byte, and notifying, every frame
            float scaled = readKnob(p, role, board.gpio[i], dt) * 255;
            if(fabsf(scaled - p.reported) > 0.75f) p.reported = lroundf(scaled);
            position = p.reported;
        }
        if(board.analogMask & bit) positions[positionCount++] = position;
    }

    RunMode mode = modeOnOpen >= 0 ? (RunMode)modeOnOpen : localPrefs.mode;
    if(holdOff) mode = Off;
    else if(holdOn) mode = On;
    if(mode != localPrefs.mode) modeProp.set(mode);
    if(holdPreset >= 0 && holdPreset != localPrefs.currentPresetIndex) presetProp.set(holdPreset);
    for(int layer = 0; layer < LAYER_COUNT; layer++)
    {
        if(switchedLayers & (1 << layer)) layerGate[layer] = switchedOnLayers & (1 << layer);
    }

    pinStatusProp.offer(encodePinStatus(wiringProp.revision, closedMask, claimedMask, positions, positionCount), dt);
    restartForLedPins(dt);
}

#endif
