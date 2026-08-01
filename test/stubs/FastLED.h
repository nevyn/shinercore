// Host stub: just enough Arduino/FastLED surface for Codecs.h and ShinyTypes.h
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
    bool equals(const char *o) const { return s == o; }
    int toInt() const { return atoi(s.c_str()); }
    const char *c_str() const { return s.c_str(); }
    String operator+(const String &o) const { return String(s + o.s); }
    String &operator+=(const String &o) { s += o.s; return *this; }
    bool operator==(const String &o) const { return s == o.s; }
    bool operator==(const char *o) const { return s == o; }
};
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }

struct CRGB {
    uint8_t r = 0, g = 0, b = 0;
    CRGB() {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
};
