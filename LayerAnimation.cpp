#include "LayerAnimation.h"
#include "BeatDetector.h"

void LayerAnimation::animate(float fraction)
{
    AnimationSettings settings = {prefs->mainColor, prefs->secondaryColor, prefs->p_tau, prefs->p_phi};
    compositeLayer(frontbuffer->leds, backbuffer->leds, frontbuffer->numPixels(), prefs->animationIndex, prefs->blendMode,
                   settings, time, beats.envelope(), gain * prefs->opacity);
}
