#include "LayerAnimation.h"
#include "Animations.h"

void LayerAnimation::animate(float absoluteTime) 
{ 
    AnimateLayerFunc func = animationFuncs[prefs->animationIndex];
    func(this, absoluteTime); 
}
