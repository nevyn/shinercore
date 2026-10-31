#include "ShinyTypes.h"

std::vector<String> blendModeNames(kBlendModeNames, kBlendModeNames + BlendModeCount);

static std::vector<String> namesOfAnimations()
{
    std::vector<String> names;
    for(int i = 0; i < AnimationCount; i++) names.push_back(kAnimations[i].name);
    return names;
}
std::vector<String> animationNames = namesOfAnimations();

std::vector<String> ledColorOrderNames = {
    "RGB",
    "GRB",
    "BGR",
};

String layerKey(const String &key, int layer, int preset)
{
    String k = key + "-" + String(layer);
    if(preset > 0)
    {
        k += "-" + String(preset);
    }
    return k;
}
