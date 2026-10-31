#include "Render.h"
#include "RenderMath.h"
#include <algorithm>

using namespace render;

namespace {

void fill(CRGB *pixels, int count, const CRGB &color)
{
    for(int i = 0; i < count; i++) pixels[i] = color;
}

void NothingAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
}

void StaticAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    fill(pixels, count, prefs.mainColor);
}

void OpposingWavesAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    for(int i = 0; i < count; i++)
    {
        pixels[i] = scale(prefs.mainColor, gammaCorrect(curve(t - i/nonzero(prefs.p_tau)))/2.0f) + scale(prefs.secondaryColor, gammaCorrect(curve(t + i/nonzero(prefs.p_phi)))/2.0f);
    }
}

// tau is waveform length, and phi is phase offset
void SingleWaveAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    for(int i = 0; i < count; i++)
    {
        pixels[i] = scale(prefs.mainColor, gammaCorrect(curve(t - i/nonzero(prefs.p_tau) + prefs.p_phi)));
    }
}

// simple fade between two colors
void BreatheAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    for(int i = 0; i < count; i++)
    {
        pixels[i] = scale(prefs.mainColor, gammaCorrect(curve(t))) + scale(prefs.secondaryColor, gammaCorrect(curve(t+0.5)));
    }
}

// Fixed rainbow: cycles hue across strip, animates over time
// tau controls how many rainbow cycles fit on the strip (higher = more rainbows)
// phi controls animation speed multiplier
void RainbowAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    float rainbowCycles = prefs.p_tau / 10.0f;
    float speedMult = prefs.p_phi / 4.0f;

    for(int i = 0; i < count; i++)
    {
        float pos = (float)i / count;
        uint8_t hue = toByte((pos * rainbowCycles + t * speedMult) * 256.0f);
        pixels[i] = CHSV(hue, 240, 255);
    }
}

// Comet/meteor: bright head with fading tail
// tau controls tail length (higher = longer tail)
// phi controls comet width
void CometAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    float tailLength = prefs.p_tau * 5.0f; // tail length in pixels
    float cometWidth = std::max(1.0f, prefs.p_phi); // head width

    float cometPos = fmod(t * count, count + tailLength);

    for(int i = 0; i < count; i++)
    {
        float distance = cometPos - i;
        if(distance >= 0 && distance < cometWidth) {
            pixels[i] = prefs.mainColor;
        } else if(distance >= cometWidth && distance < tailLength + cometWidth) {
            float fade = 1.0f - (distance - cometWidth) / tailLength;
            fade = fade * fade; // quadratic falloff for nice tail
            pixels[i] = scale(prefs.secondaryColor, fade);
        } else {
            pixels[i] = CRGB::Black;
        }
    }
}

// Cylon/Knight Rider scanner - light bounces back and forth
// tau controls scanner width
// phi controls how much the scanner "bleeds" (glow width)
void ScannerAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    float scannerWidth = std::max(1.0f, prefs.p_tau);
    float glowWidth = prefs.p_phi;

    float scannerPos = curve(t) * (count-1);

    for(int i = 0; i < count; i++)
    {
        float distance = fabs(i - scannerPos);
        if(distance < scannerWidth / 2.0f) {
            pixels[i] = prefs.mainColor;
        } else if(distance < scannerWidth / 2.0f + glowWidth) {
            float fade = 1.0f - (distance - scannerWidth / 2.0f) / glowWidth;
            pixels[i] = scale(prefs.secondaryColor, fade * fade);
        } else {
            pixels[i] = CRGB::Black;
        }
    }
}

// Twinkle: stateless random stars using hash function
// tau controls twinkle density (how many stars)
// phi controls twinkle speed
void TwinkleAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    float density = prefs.p_tau / 10.0f; // 0-1 ish
    float speed = prefs.p_phi;

    for(int i = 0; i < count; i++)
    {
        // Each pixel gets its own "random" phase and frequency
        float phase = hashFloat(i * 12345);
        float freq = 0.5f + hashFloat(i * 67890) * speed;

        float starChance = hashFloat(i * 11111);
        if(starChance > density) {
            pixels[i] = CRGB::Black;
            continue;
        }

        float brightness = curve(t * freq + phase);
        brightness = brightness * brightness; // sharper twinkle

        CRGB color = (hash(i) & 1) ? prefs.mainColor : prefs.secondaryColor;
        pixels[i] = scale(color, brightness);
    }
}

// Theater chase / marquee lights
// tau controls spacing between lit pixels
// phi controls group size (how many lit in a row)
void TheaterChaseAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    int spacing = std::max(2, (int)prefs.p_tau);
    int groupSize = std::max(1, (int)prefs.p_phi);

    int offset = toInt(t * spacing * 2) % spacing;

    for(int i = 0; i < count; i++)
    {
        int posInPattern = (i + offset) % spacing;
        if(posInPattern < groupSize) {
            // Alternate colors for each group
            int groupNum = (i + offset) / spacing;
            pixels[i] = (groupNum & 1) ? prefs.mainColor : prefs.secondaryColor;
        } else {
            pixels[i] = CRGB::Black;
        }
    }
}

// Color wipe: fills the strip with the primary color, then the secondary
// tau controls wipe speed
void ColorWipeAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    // Full cycle: fill with primary, pause, fill with secondary, pause
    float cycleLen = 4.0f; // seconds per full cycle
    float phase = fmod(t / (prefs.p_tau / 10.0f + 0.5f), cycleLen);

    CRGB fillColor;
    float fillAmount;

    if(phase < 1.0f) {
        fillAmount = phase;
        fillColor = prefs.mainColor;
        for(int i = 0; i < count; i++) {
            float pos = (float)i / count;
            pixels[i] = (pos < fillAmount) ? fillColor : CRGB::Black;
        }
    } else if(phase < 2.0f) {
        fill(pixels, count, prefs.mainColor);
    } else if(phase < 3.0f) {
        fillAmount = phase - 2.0f;
        for(int i = 0; i < count; i++) {
            float pos = (float)i / count;
            pixels[i] = (pos < fillAmount) ? prefs.secondaryColor : prefs.mainColor;
        }
    } else {
        fill(pixels, count, prefs.secondaryColor);
    }
}

// Gradient pulse: smooth gradient between colors that shifts over time
// tau controls gradient steepness
// phi controls number of gradient cycles on strip
void GradientPulseAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    float cycles = prefs.p_phi / 4.0f; // how many gradients fit on strip
    float sharpness = prefs.p_tau / 10.0f; // 0=smooth sine, higher=sharper

    for(int i = 0; i < count; i++)
    {
        float pos = (float)i / count;
        float wave = curve(pos * cycles + t);

        // Apply sharpness (power function makes transitions sharper)
        if(sharpness > 0) {
            wave = powf(wave, 1.0f / (sharpness + 1.0f));
        }

        pixels[i] = prefs.mainColor.lerp8(prefs.secondaryColor, (uint8_t)(wave * 255));
    }
}

// Sparkle: random bright flashes on a dim background
// tau controls flash duration
// phi controls flash density
void SparkleAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    float flashDuration = 0.05f + prefs.p_tau / 100.0f; // how long each flash lasts
    float density = prefs.p_phi / 20.0f; // chance per pixel per "slot"

    // Background color (dim version of secondary)
    CRGB bgColor = scale(prefs.secondaryColor, 0.1f);

    for(int i = 0; i < count; i++)
    {
        // Divide time into slots, check if this pixel sparkles in current/recent slots
        float brightness = 0;

        for(int slot = 0; slot < 3; slot++) {
            int timeSlot = toInt(t / flashDuration) - slot;
            uint32_t seed = hash(i * 99999 + timeSlot);
            float chance = (seed & 0xFFFF) / 65535.0f;

            if(chance < density) {
                float slotStart = timeSlot * flashDuration;
                float slotProgress = (t - slotStart) / flashDuration;
                if(slotProgress >= 0 && slotProgress < 1.0f) {
                    // Fade in then out
                    float flash = (slotProgress < 0.5f)
                        ? (slotProgress * 2.0f)
                        : (2.0f - slotProgress * 2.0f);
                    brightness = std::max(brightness, flash);
                }
            }
        }

        if(brightness > 0) {
            pixels[i] = scale(prefs.mainColor, brightness);
        } else {
            pixels[i] = bgColor;
        }
    }
}

// Whole strip flashes mainColor on every detected beat (needs the mic setting
// on and an M5Atom Echo). Layer it over other animations with Add or Screen.
void BeatPulseAnim(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat)
{
    fill(pixels, count, scale(prefs.mainColor, beat));
}

}

const AnimationInfo kAnimations[] = {
    {"Nothing", NothingAnim, 0, NULL, NULL},
    {"Static", StaticAnim, 1, NULL, NULL},
    {"Opposing Waves", OpposingWavesAnim, 2, "Wave length", "Counter-wave length"},
    {"Single Wave", SingleWaveAnim, 1, "Wave length", "Phase"},
    {"Breathe", BreatheAnim, 2, NULL, NULL},
    {"Rainbow", RainbowAnim, 0, "Rainbows", "Drift speed"},
    {"Comet", CometAnim, 2, "Tail length", "Head width"},
    {"Scanner", ScannerAnim, 2, "Width", "Glow"},
    {"Twinkle", TwinkleAnim, 2, "Density", "Twinkle speed"},
    {"Theater Chase", TheaterChaseAnim, 2, "Spacing", "Group size"},
    {"Color Wipe", ColorWipeAnim, 2, "Wipe time", NULL},
    {"Gradient Pulse", GradientPulseAnim, 2, "Sharpness", "Repeats"},
    {"Sparkle", SparkleAnim, 2, "Flash length", "Density"},
    {"Beat Pulse", BeatPulseAnim, 1, NULL, NULL},
};
static_assert(sizeof(kAnimations) / sizeof(*kAnimations) == AnimationCount, "AnimationCount must match the table");
