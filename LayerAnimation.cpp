#include "LayerAnimation.h"
#include "Animations.h"

void LayerAnimation::animate(float absoluteTime) 
{ 
    AnimateLayerFunc func = animationFuncs[prefs->animationIndex];
    backbuffer->fill(CRGB::Black);
    func(this, absoluteTime); 

    for(int i = 0; i < frontbuffer->numPixels(); i++)
    {
        CRGB a = (*backbuffer)[i];
        CRGB b = (*frontbuffer)[i];
        CRGB blended;
        switch(prefs->blendMode) {
            case BlendModeAdd: blended = a + b; break;
        }
        frontbuffer->set(i, blended);
    }
}
