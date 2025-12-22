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
      : Animation(1.0, true), backbuffer(backbuffer), frontbuffer(frontbuffer), prefs(prefs), _accumulated(0), _lastFraction(1)
      {}
protected:
    void animate(float fraction);

    // xx hack: I thought animate took time, but it actually takes fraction. calculate time so we can keep it accumulating
    TimeInterval _accumulated;
    float _lastFraction;
};

typedef void(*AnimateLayerFunc)(LayerAnimation*, TimeInterval);


#endif