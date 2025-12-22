#include "Animations.h"
#include "Util.h"

// Simple hash for deterministic pseudo-random based on position/time
// Allows "random" effects to be stateless
static inline uint32_t hash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

// Returns pseudo-random float 0-1 for a given seed
static inline float hashFloat(uint32_t seed) {
    return (hash(seed) & 0xFFFF) / 65535.0f;
}

void NothingAnim(LayerAnimation *self, float t)
{
}

void OpposingWavesAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    for(int i = 0; i < strip->numPixels(); i++)
    {
        strip->set(i, prefs->mainColor * (gammaf(curve(t - i/prefs->p_tau))/2.0f) + prefs->secondaryColor * (gammaf(curve(t + i/prefs->p_phi))/2.0f));
    }
}

// tau is waveform length, and phi is phase offset
void SingleWaveAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    for(int i = 0; i < strip->numPixels(); i++)
    {
        strip->set(i, prefs->mainColor * (gammaf(curve(t - i/prefs->p_tau + prefs->p_phi))));
    }
}

// simple fade between two colors
void BreatheAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    for(int i = 0; i < strip->numPixels(); i++)
    {
        strip->set(i, prefs->mainColor * gammaf(curve(t)) + prefs->secondaryColor * gammaf(curve(t+0.5)));
    }
}

// Fixed rainbow: cycles hue across strip, animates over time
// tau controls how many rainbow cycles fit on the strip (higher = more rainbows)
// phi controls animation speed multiplier
void RainbowAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    // tau controls rainbow density (cycles per strip), default around 1.0
    float rainbowCycles = prefs->p_tau / 10.0f;
    // phi controls speed, default around 1.0
    float speedMult = prefs->p_phi / 4.0f;
    
    for(int i = 0; i < numPixels; i++)
    {
        // Normalized position 0-1 along strip
        float pos = (float)i / numPixels;
        // Hue: position-based + time-based animation
        // Multiply by 256 to get full 8-bit hue range
        uint8_t hue = (uint8_t)((pos * rainbowCycles + t * speedMult) * 256.0f);
        strip->set(i, CHSV(hue, 240, 255));
    }
}

// Comet/meteor: bright head with fading tail
// tau controls tail length (higher = longer tail)
// phi controls comet width
void CometAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    float tailLength = prefs->p_tau * 5.0f; // tail length in pixels
    float cometWidth = std::max(1.0f, prefs->p_phi); // head width
    
    // Comet position cycles through strip
    float cometPos = fmod(t * numPixels, numPixels + tailLength);
    
    for(int i = 0; i < numPixels; i++)
    {
        float distance = cometPos - i;
        if(distance >= 0 && distance < cometWidth) {
            // Bright head
            strip->set(i, prefs->mainColor);
        } else if(distance >= cometWidth && distance < tailLength + cometWidth) {
            // Fading tail
            float fade = 1.0f - (distance - cometWidth) / tailLength;
            fade = fade * fade; // quadratic falloff for nice tail
            strip->set(i, prefs->secondaryColor * fade);
        } else {
            strip->set(i, CRGB::Black);
        }
    }
}

// Cylon/Knight Rider scanner - light bounces back and forth
// tau controls scanner width
// phi controls how much the scanner "bleeds" (glow width)
void ScannerAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    float scannerWidth = std::max(1.0f, prefs->p_tau);
    float glowWidth = prefs->p_phi;
    
    float scannerPos = curve(t) * (numPixels-1);
    
    for(int i = 0; i < numPixels; i++)
    {
        float distance = fabs(i - scannerPos);
        if(distance < scannerWidth / 2.0f) {
            // Core of scanner
            strip->set(i, prefs->mainColor);
        } else if(distance < scannerWidth / 2.0f + glowWidth) {
            // Glow falloff
            float fade = 1.0f - (distance - scannerWidth / 2.0f) / glowWidth;
            strip->set(i, prefs->secondaryColor * (fade * fade));
        } else {
            strip->set(i, CRGB::Black);
        }
    }
}

// Twinkle: stateless random stars using hash function
// tau controls twinkle density (how many stars)
// phi controls twinkle speed
void TwinkleAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    float density = prefs->p_tau / 10.0f; // 0-1 ish
    float speed = prefs->p_phi;
    
    for(int i = 0; i < numPixels; i++)
    {
        // Each pixel gets its own "random" phase and frequency
        float phase = hashFloat(i * 12345);
        float freq = 0.5f + hashFloat(i * 67890) * speed;
        
        // Determine if this pixel is a "star" based on density
        float starChance = hashFloat(i * 11111);
        if(starChance > density) {
            strip->set(i, CRGB::Black);
            continue;
        }
        
        // Twinkle using sine wave with per-pixel phase
        float brightness = curve(t * freq + phase);
        brightness = brightness * brightness; // sharper twinkle
        
        // Alternate between primary and secondary color based on position
        CRGB color = (hash(i) & 1) ? prefs->mainColor : prefs->secondaryColor;
        strip->set(i, color * brightness);
    }
}

// Theater chase / marquee lights
// tau controls spacing between lit pixels
// phi controls group size (how many lit in a row)
void TheaterChaseAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    int spacing = std::max(2, (int)prefs->p_tau);
    int groupSize = std::max(1, (int)prefs->p_phi);
    
    // Animate the offset
    int offset = (int)(t * spacing * 2) % spacing;
    
    for(int i = 0; i < numPixels; i++)
    {
        int posInPattern = (i + offset) % spacing;
        if(posInPattern < groupSize) {
            // Alternate colors for each group
            int groupNum = (i + offset) / spacing;
            strip->set(i, (groupNum & 1) ? prefs->mainColor : prefs->secondaryColor);
        } else {
            strip->set(i, CRGB::Black);
        }
    }
}

// Color wipe: fills strip with color, then clears
// tau controls wipe speed
// phi controls pause time at full/empty
void ColorWipeAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    // Full cycle: fill with primary, pause, fill with secondary, pause
    float cycleLen = 4.0f; // seconds per full cycle
    float phase = fmod(t / (prefs->p_tau / 10.0f + 0.5f), cycleLen);
    
    CRGB fillColor;
    float fillAmount;
    
    if(phase < 1.0f) {
        // Filling with primary
        fillAmount = phase;
        fillColor = prefs->mainColor;
        for(int i = 0; i < numPixels; i++) {
            float pos = (float)i / numPixels;
            strip->set(i, (pos < fillAmount) ? fillColor : CRGB::Black);
        }
    } else if(phase < 2.0f) {
        // Pause at full primary
        for(int i = 0; i < numPixels; i++) {
            strip->set(i, prefs->mainColor);
        }
    } else if(phase < 3.0f) {
        // Filling with secondary (replacing primary)
        fillAmount = phase - 2.0f;
        for(int i = 0; i < numPixels; i++) {
            float pos = (float)i / numPixels;
            strip->set(i, (pos < fillAmount) ? prefs->secondaryColor : prefs->mainColor);
        }
    } else {
        // Pause at full secondary
        for(int i = 0; i < numPixels; i++) {
            strip->set(i, prefs->secondaryColor);
        }
    }
}

// Gradient pulse: smooth gradient between colors that shifts over time
// tau controls gradient steepness
// phi controls number of gradient cycles on strip
void GradientPulseAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    float cycles = prefs->p_phi / 4.0f; // how many gradients fit on strip
    float sharpness = prefs->p_tau / 10.0f; // 0=smooth sine, higher=sharper
    
    for(int i = 0; i < numPixels; i++)
    {
        float pos = (float)i / numPixels;
        // Wave with position and time
        float wave = curve(pos * cycles + t);
        
        // Apply sharpness (power function makes transitions sharper)
        if(sharpness > 0) {
            wave = powf(wave, 1.0f / (sharpness + 1.0f));
        }
        
        // Blend between colors
        strip->set(i, prefs->mainColor.lerp8(prefs->secondaryColor, (uint8_t)(wave * 255)));
    }
}

// Sparkle: random bright flashes on a dim background
// tau controls flash duration
// phi controls flash density
void SparkleAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;
    int numPixels = strip->numPixels();
    
    float flashDuration = 0.05f + prefs->p_tau / 100.0f; // how long each flash lasts
    float density = prefs->p_phi / 20.0f; // chance per pixel per "slot"
    
    // Background color (dim version of secondary)
    CRGB bgColor = prefs->secondaryColor * 0.1f;
    
    for(int i = 0; i < numPixels; i++)
    {
        // Divide time into slots, check if this pixel sparkles in current/recent slots
        float brightness = 0;
        
        // Check a few time slots for sparkles
        for(int slot = 0; slot < 3; slot++) {
            int timeSlot = (int)(t / flashDuration) - slot;
            uint32_t seed = hash(i * 99999 + timeSlot);
            float chance = (seed & 0xFFFF) / 65535.0f;
            
            if(chance < density) {
                // This pixel sparkles in this slot
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
            strip->set(i, prefs->mainColor * brightness);
        } else {
            strip->set(i, bgColor);
        }
    }
}

extern std::vector<String> animationNames = {
    "Nothing",
    "Opposing Waves",
    "Single Wave",
    "Breathe",
    "Rainbow",
    "Comet",
    "Scanner",
    "Twinkle",
    "Theater Chase",
    "Color Wipe",
    "Gradient Pulse",
    "Sparkle",
};
std::vector<AnimateLayerFunc> animationFuncs = {
    NothingAnim,
    OpposingWavesAnim,
    SingleWaveAnim,
    BreatheAnim,
    RainbowAnim,
    CometAnim,
    ScannerAnim,
    TwinkleAnim,
    TheaterChaseAnim,
    ColorWipeAnim,
    GradientPulseAnim,
    SparkleAnim,
};
