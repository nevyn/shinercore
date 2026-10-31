// Host stub: just enough Arduino/FastLED surface for Codecs.h, ShinyTypes.h and Wiring.h
#pragma once
#include "../../src/render/host/FastLED.h"
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

class String {
public:
    std::string s;
    String() {}
    String(const char *v) : s(v) {}
    String(const std::string &v) : s(v) {}
    String(int v) : s(std::to_string(v)) {}
    String(long v) : s(std::to_string(v)) {}
    String(uint8_t v) : s(std::to_string((int)v)) {}
    String(float v, int decimals = 2) { char buf[32]; snprintf(buf, sizeof buf, "%.*f", decimals, v); s = buf; }
    bool isEmpty() const { return s.empty(); }
    unsigned length() const { return s.size(); }
    bool equals(const char *o) const { return s == o; }
    int toInt() const { return atoi(s.c_str()); }
    const char *c_str() const { return s.c_str(); }
    String operator+(const String &o) const { return String(s + o.s); }
    String &operator+=(const String &o) { s += o.s; return *this; }
    bool operator==(const String &o) const { return s == o.s; }
    bool operator==(const char *o) const { return s == o; }
    bool operator!=(const String &o) const { return s != o.s; }
};
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }

inline CHSV rgb2hsv_approximate(const CRGB &c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    if(mx == 0 || mx == mn) return CHSV(0, 0, mx);
    int s = 255 * (mx - mn) / mx, h;
    if(mx == c.r) h = 43 * (c.g - c.b) / (mx - mn);
    else if(mx == c.g) h = 85 + 43 * (c.b - c.r) / (mx - mn);
    else h = 171 + 43 * (c.r - c.g) / (mx - mn);
    return CHSV((uint8_t)h, s, mx);
}
