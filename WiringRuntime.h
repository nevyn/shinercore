#ifndef __WIRING_RUNTIME__H
#define __WIRING_RUNTIME__H
// Runs the wiring: registers LED outputs at boot, then reads the pins every frame
// and acts on their roles (docs/wiring.md, Runtime semantics). Writes settings
// through the properties, so the sketch includes it after Comms.h.

static const float kDebounceSeconds = 0.03;
static const float kLongPressSeconds = 0.6;    // a nextPreset button held this long toggles the lights
static const float kLedRestartDelaySeconds = 0.3; // lets the wiring write's response reach the app first

void cycleToNextPreset();
void toggleMode();

// What the runtime has configured a pin as, so it reconfigures only on change
enum PinDrive : uint8_t { DriveNone, DriveUntouched, DrivePullup };

struct PinRuntime
{
    PinRole role;
    PinDrive drive = DriveNone;
    bool seeded = false;
    bool raw = false, closed = false; // closed means pulled to G, debounced
    float rawFor = 0;
    float heldFor = 0;
    bool longPressFired = false;
};
PinRuntime pinRuntime[WIRING_MAX_PINS];
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
    if(wiringDrivesLeds(GROVE1_PIN)) { FastLED.addLeds<WS2811, GROVE1_PIN, RGB>(rgbs, MAX_LED_COUNT); stripOutputs++; }
    if(wiringDrivesLeds(GROVE2_PIN)) { FastLED.addLeds<WS2811, GROVE2_PIN, RGB>(rgbs, MAX_LED_COUNT); stripOutputs++; }
    registeredLedPins = localPrefs.wiring.ledPins();
}

// True when the debounced state changed this frame
static bool debounce(PinRuntime &p, bool raw, float dt)
{
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
        default: break;
    }
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

    for(int i = 0; i < board.pinCount; i++)
    {
        const PinRole &role = localPrefs.wiring.pins[i];
        PinRuntime &p = pinRuntime[i];
        uint8_t bit = 1 << i;
        bool claimed = (board.claimMask & bit) || ((board.micClaimMask & bit) && localPrefs.micEnabled);
        bool leds = (registeredLedPins & bit) || role.role == RoleLeds;
        PinDrive drive = (claimed || leds) ? DriveUntouched : DrivePullup;
        if(claimed) claimedMask |= bit;

        if(drive != p.drive || role != p.role)
        {
            // A new role, or a claim that ended, starts from rest
            p = PinRuntime();
            p.role = role;
            p.drive = drive;
            if(drive == DrivePullup) pinMode(board.gpio[i], INPUT_PULLUP);
        }

        uint8_t position = 0;
        if(drive == DrivePullup)
        {
            bool raw = digitalRead(board.gpio[i]) == LOW;
            bool changed = false;
            if(!p.seeded)
            {
                // No edge on the first reading: a button that's already held does nothing until pressed again
                p.raw = p.closed = p.longPressFired = raw;
                p.seeded = true;
            }
            else changed = debounce(p, raw, dt);
            if(p.closed) closedMask |= bit;
            position = p.closed ? 0x00 : 0xff;

            if(role.role == RoleSwitch)
            {
                switch(role.action)
                {
                    case SwitchLightsOff: if(p.closed) holdOff = true; else if(changed) modeOnOpen = On; break;
                    case SwitchLightsOn: if(p.closed) holdOn = true; else if(changed) modeOnOpen = Off; break;
                    case SwitchPreset: if(p.closed && holdPreset < 0) holdPreset = role.preset; break;
                    default: break;
                }
            }
            else if(role.role == RoleButton && role.action == ButtonNextPreset)
            {
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
            else if(role.role == RoleButton && changed && p.closed) pressButton(role);
        }
        if(board.analogMask & bit) positions[positionCount++] = position;
    }

    RunMode mode = modeOnOpen >= 0 ? (RunMode)modeOnOpen : localPrefs.mode;
    if(holdOff) mode = Off;
    else if(holdOn) mode = On;
    if(mode != localPrefs.mode) modeProp.set(mode);
    if(holdPreset >= 0 && holdPreset != localPrefs.currentPresetIndex) presetProp.set(holdPreset);

    pinStatusProp.offer(encodePinStatus(wiringProp.revision, closedMask, claimedMask, positions, positionCount), dt);
    restartForLedPins(dt);
}

#endif
