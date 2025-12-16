#include "Animations.h"
#include "Util.h"

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

// TODO new animations:
// sätt random leds
// flyg fram en färg i taget, långsamt till snabbt
// cylon
// rainbow

// Port of Fire2012 by Mark Kriegsman
// Adapted from https://github.com/bportaluri/ALA/blob/master/src/AlaLedRgb.cpp#L649 :) 
void FireAnim(LayerAnimation *self, float t)
{
    SubStrip *strip = self->backbuffer;
    ShinyLayerSettings *prefs = self->prefs;

    // does not work well with 400...
    static const int numPixels = 100;

    // COOLING: How much does the air cool as it rises?
    // Less cooling = taller flames.  More cooling = shorter flames.
    // Default 550
    #define COOLING  600

    // SPARKING: What chance (out of 255) is there that a new spark will be lit?
    // Higher chance = more roaring fire.  Lower chance = more flickery fire.
    // Default 120, suggested range 50-200.
    #define SPARKING 120

    // Array of temperature readings at each simulation cell
    static byte heat[numPixels];

    // Step 1.  Cool down every cell a little
    int rMax = (COOLING / numPixels) + 2;
    for(int i=0; i<numPixels; i++)
    {
        heat[i] = std::max(((int)heat[i]) - (int)random(0, rMax), 0);
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for(int k=numPixels-1; k>=3; k--)
    {
        heat[k] = ((int)heat[k - 1] + (int)heat[k - 2] + (int)heat[k - 3] ) / 3;
    }

    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if(random(255) < SPARKING)
    {
        int y = random(7);
        heat[y] = std::min(heat[y] + (int)random(160, 255), 255);
    }

    // Step 4.  Map from heat cells to LED colors
    for(int j=0; j<numPixels; j++)
    {
        CRGB color = prefs->mainColor.lerp8(prefs->secondaryColor, heat[j]);
        strip->set(j, color);
    }
}


std::vector<AnimateLayerFunc> animationFuncs = {NothingAnim, OpposingWavesAnim, BreatheAnim, FireAnim};
