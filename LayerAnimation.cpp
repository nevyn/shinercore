#include "LayerAnimation.h"
#include "Animations.h"

void LayerAnimation::animate(float fraction) 
{
    // if we wrap over to 0, assume another full second has passed
    if(fraction < _lastFraction)
    {
        _accumulated += 1.0;
    }
    _lastFraction = fraction;
    TimeInterval absoluteTime = _accumulated + fraction;

    AnimateLayerFunc func = animationFuncs[prefs->animationIndex];
    backbuffer->fill(CRGB::Black);

    if(prefs->animationIndex == 0) return; // NoAnimation? do nothing, don't waste time filling and blending.

    func(this, absoluteTime); 

    for(int i = 0; i < frontbuffer->numPixels(); i++)
    {
        CRGB a = (*frontbuffer)[i]; // current contents on the strip
        CRGB b = (*backbuffer)[i]; // this layer's new content to be blended in

        CRGB blended;
        switch(prefs->blendMode) {
            case BlendModeAdd: default: blended = a + b; break;
            case BlendModeSubtract: blended = a - b; break;
            case BlendModeAddWrap: blended = CRGB((a.r + b.r) & 0xFF, (a.g + b.g) & 0xFF, (a.b + b.b) & 0xFF); break;
            case BlendModeSubtractWrap: blended = CRGB((a.r - b.r) & 0xFF, (a.g - b.g) & 0xFF, (a.b - b.b) & 0xFF); break;
            case BlendModeMultiply: blended = CRGB((a.r * b.r) >> 8, (a.g * b.g) >> 8, (a.b * b.b) >> 8); break;
            case BlendModeDissolve: blended = random8() < 128 ? a : b; break;
            case BlendModeAverage: blended = CRGB((a.r + b.r) >> 1, (a.g + b.g) >> 1, (a.b + b.b) >> 1); break;
            case BlendModeSet: blended = b; break;
            case BlendModeScreen: blended = CRGB(255 - (((255-a.r) * (255-b.r)) >> 8),
                                                 255 - (((255-a.g) * (255-b.g)) >> 8),
                                                 255 - (((255-a.b) * (255-b.b)) >> 8)); break;
            case BlendModeLighten: blended = CRGB(max(a.r, b.r), max(a.g, b.g), max(a.b, b.b)); break;
            case BlendModeDarken: blended = CRGB(min(a.r, b.r), min(a.g, b.g), min(a.b, b.b)); break;
            case BlendModeDifference: blended = CRGB(abs(a.r - b.r), abs(a.g - b.g), abs(a.b - b.b)); break;
            case BlendModeOverlay: blended = CRGB(
                a.r < 128 ? (2 * a.r * b.r) >> 8 : 255 - ((2 * (255-a.r) * (255-b.r)) >> 8),
                a.g < 128 ? (2 * a.g * b.g) >> 8 : 255 - ((2 * (255-a.g) * (255-b.g)) >> 8),
                a.b < 128 ? (2 * a.b * b.b) >> 8 : 255 - ((2 * (255-a.b) * (255-b.b)) >> 8)); break;
            case BlendModeColorDodge: blended = CRGB(
                b.r == 255 ? 255 : min(255, (a.r << 8) / (255 - b.r)),
                b.g == 255 ? 255 : min(255, (a.g << 8) / (255 - b.g)),
                b.b == 255 ? 255 : min(255, (a.b << 8) / (255 - b.b))); break;
        }
        frontbuffer->set(i, blended);
    }
}
