#include "ShinyTypes.h"

std::vector<String> blendModeNames(kBlendModeNames, kBlendModeNames + BlendModeCount);

static std::vector<String> namesOfAnimations()
{
    std::vector<String> names;
    for(int i = 0; i < AnimationCount; i++) names.push_back(kAnimations[i].name);
    return names;
}
std::vector<String> animationNames = namesOfAnimations();

String animationDocsJSON()
{
    String json = "[";
    for(int i = 0; i < AnimationCount; i++)
    {
        const AnimationInfo &a = kAnimations[i];
        if(i > 0) json += ",";
        json += "[" + String(a.colors);
        if(a.tauName || a.phiName) json += ",\"" + String(a.tauName ? a.tauName : "") + "\"";
        if(a.phiName) json += ",\"" + String(a.phiName) + "\"";
        json += "]";
    }
    return json + "]";
}

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
