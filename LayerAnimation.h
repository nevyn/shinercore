#ifndef __LAYER_ANIMATION__H
#define __LAYER_ANIMATION__H
#include <OverAnimate.h>
#include <SubStrip.h>
#include "ShinyTypes.h"

class LayerAnimation : public Animation
{
public:
    SubStrip *backbuffer;
    SubStrip *frontbuffer;
    ShinyLayerSettings *prefs;
    LayerAnimation(SubStrip *backbuffer, SubStrip *frontbuffer, ShinyLayerSettings *prefs) 
      : Animation(1.0, true), backbuffer(backbuffer), frontbuffer(frontbuffer), prefs(prefs) {}
protected:
    void animate(float absoluteTime);
};

typedef void(*AnimateLayerFunc)(LayerAnimation*, float);


#endif