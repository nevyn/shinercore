#include "ShinyTypes.h"

std::vector<String> blendModeNames = {
    "Add",
    "Subtract",
    "Add Wrap",
    "Subtract Wrap",
    "Multiply",
    "Dissolve",
    "Average",
    "Set",
    "Screen",
    "Lighten",
    "Darken",
    "Difference",
    "Overlay",
    "Color Dodge",
};

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
