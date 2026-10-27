// Host stub: just enough Arduino/FastLED surface for Codecs.h, ShinyTypes.h and Wiring.h
#pragma once
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

struct CHSV {
    uint8_t h = 0, s = 0, v = 0;
    CHSV() {}
    CHSV(uint8_t h, uint8_t s, uint8_t v) : h(h), s(s), v(v) {}
    uint8_t &operator[](int i) { return i == 0 ? h : i == 1 ? s : v; }
};

struct CRGB {
    uint8_t r = 0, g = 0, b = 0;
    CRGB() {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    // Plain integer HSV, standing in for hsv2rgb_rainbow: lossy like it
    CRGB(const CHSV &c) {
        int region = c.h / 43, rem = (c.h - region * 43) * 6;
        uint8_t p = (c.v * (255 - c.s)) >> 8, q = (c.v * (255 - ((c.s * rem) >> 8))) >> 8,
                t = (c.v * (255 - ((c.s * (255 - rem)) >> 8))) >> 8;
        switch(region) {
            case 0: r = c.v; g = t; b = p; break;
            case 1: r = q; g = c.v; b = p; break;
            case 2: r = p; g = c.v; b = t; break;
            case 3: r = p; g = q; b = c.v; break;
            case 4: r = t; g = p; b = c.v; break;
            default: r = c.v; g = p; b = q; break;
        }
    }
    uint8_t &operator[](int i) { return i == 0 ? r : i == 1 ? g : b; }
    bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const CRGB &o) const { return !(*this == o); }
};

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
