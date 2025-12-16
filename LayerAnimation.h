#ifndef __LAYER_ANIMATION__H
#define __LAYER_ANIMATION__H
#include <OverAnimate.h>
#include <SubStrip.h>
#include "ShinyTypes.h"

class LayerAnimation : public Animation
{
public:
    SubStrip *backbuffer;
    ProxyStrip *frontbuffer;
    ShinyLayerSettings *prefs;
    LayerAnimation(SubStrip *backbuffer, ProxyStrip *frontbuffer, ShinyLayerSettings *prefs, TimeInterval duration = 1.0, bool repeats = false) 
      : Animation(duration, repeats), backbuffer(backbuffer), frontbuffer(frontbuffer), prefs(prefs) {}
protected:
    void animate(float absoluteTime);
};

typedef void(*AnimateLayerFunc)(LayerAnimation*, float);


#endif