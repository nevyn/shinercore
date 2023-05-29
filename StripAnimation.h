typedef void(*StripFunc)(Animation*, SubStrip *strip, ShinySettings *prefs, float);
class StripAnimation : public Animation
{
public:
    StripFunc func;
    SubStrip *strip;
    ShinySettings *prefs;
    StripAnimation(StripFunc func, SubStrip *strip, TimeInterval duration = 1.0, bool repeats = false) 
      : Animation(duration, repeats), func(func), strip(strip) {}
protected:
    void animate(float absoluteTime) { func(this, strip, prefs, absoluteTime); }
};
