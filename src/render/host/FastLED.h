// Host stand-in for FastLED: the CRGB/CHSV surface that the render library uses,
// with FastLED 3.6's arithmetic on ESP32 (its C paths, FASTLED_SCALE8_FIXED and
// FASTLED_BLEND_FIXED on). Fidelity rule: docs/rendering.md.
//
// The arithmetic below is copied from FastLED (lib8tion, colorutils.cpp, hsv2rgb.cpp):
//
// The MIT License (MIT)
//
// Copyright (c) 2013 FastLED
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#ifndef SHINER_RENDER_HOST_FASTLED_H
#define SHINER_RENDER_HOST_FASTLED_H
#include <stdint.h>

typedef uint8_t fract8;

inline uint8_t qadd8(uint8_t i, uint8_t j)
{
    unsigned int t = i + j;
    if(t > 255) t = 255;
    return t;
}

inline uint8_t qsub8(uint8_t i, uint8_t j)
{
    int t = i - j;
    if(t < 0) t = 0;
    return t;
}

inline uint8_t scale8(uint8_t i, fract8 scale)
{
    return (((uint16_t)i) * (1+(uint16_t)(scale))) >> 8;
}

inline uint8_t scale8_video(uint8_t i, fract8 scale)
{
    uint8_t j = (((int)i * (int)scale) >> 8) + ((i&&scale)?1:0);
    return j;
}

inline uint8_t blend8(uint8_t a, uint8_t b, uint8_t amountOfB)
{
    uint16_t partial;
    uint8_t result;
    partial = (a << 8) | b;
    partial += (b * amountOfB);
    partial -= (a * amountOfB);
    result = partial >> 8;
    return result;
}

inline uint8_t lerp8by8(uint8_t a, uint8_t b, fract8 frac)
{
    uint8_t result;
    if( b > a) {
        uint8_t delta = b - a;
        uint8_t scaled = scale8( delta, frac);
        result = a + scaled;
    } else {
        uint8_t delta = a - b;
        uint8_t scaled = scale8( delta, frac);
        result = a - scaled;
    }
    return result;
}

// FastLED keeps this in a global defined in lib8tion.cpp; a header-only stub needs a function.
inline uint16_t &rand16seed_ref()
{
    static uint16_t seed = 1337;
    return seed;
}

inline uint8_t random8()
{
    uint16_t &rand16seed = rand16seed_ref();
    rand16seed = (rand16seed * ((uint16_t)(2053))) + ((uint16_t)(13849));
    return (uint8_t)(((uint8_t)(rand16seed & 0xFF)) + ((uint8_t)(rand16seed >> 8)));
}

inline void random16_set_seed(uint16_t seed)
{
    rand16seed_ref() = seed;
}

struct CHSV {
    uint8_t h = 0, s = 0, v = 0;
    CHSV() {}
    CHSV(uint8_t h, uint8_t s, uint8_t v) : h(h), s(s), v(v) {}
    uint8_t &operator[](int i) { return i == 0 ? h : i == 1 ? s : v; }
};

struct CRGB;
inline void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb);

struct CRGB {
    uint8_t r = 0, g = 0, b = 0;
    enum HTMLColorCode { Black = 0x000000 };
    CRGB() {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    CRGB(HTMLColorCode) {}
    CRGB(const CHSV &rhs) { hsv2rgb_rainbow(rhs, *this); }
    CRGB lerp8(const CRGB &other, fract8 frac) const
    {
        return CRGB(lerp8by8(r, other.r, frac), lerp8by8(g, other.g, frac), lerp8by8(b, other.b, frac));
    }
    uint8_t &operator[](int i) { return i == 0 ? r : i == 1 ? g : b; }
    bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const CRGB &o) const { return !(*this == o); }
};

inline CRGB operator+(const CRGB &p1, const CRGB &p2)
{
    return CRGB(qadd8(p1.r, p2.r), qadd8(p1.g, p2.g), qadd8(p1.b, p2.b));
}

inline CRGB operator-(const CRGB &p1, const CRGB &p2)
{
    return CRGB(qsub8(p1.r, p2.r), qsub8(p1.g, p2.g), qsub8(p1.b, p2.b));
}

inline CRGB blend(const CRGB &p1, const CRGB &p2, fract8 amountOfP2)
{
    if(amountOfP2 == 0) return p1;
    if(amountOfP2 == 255) return p2;
    return CRGB(blend8(p1.r, p2.r, amountOfP2), blend8(p1.g, p2.g, amountOfP2), blend8(p1.b, p2.b, amountOfP2));
}

inline void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb)
{
    const uint8_t K255 = 255, K171 = 171, K170 = 170, K85 = 85;
    uint8_t hue = hsv.h;
    uint8_t sat = hsv.s;
    uint8_t val = hsv.v;

    uint8_t offset = hue & 0x1F; // 0..31
    uint8_t offset8 = offset;
    offset8 <<= 3;

    uint8_t third = scale8( offset8, (256 / 3)); // max = 85
    uint8_t r, g, b;

    // Y1 on, Y2, G2 and Gscale off: FastLED's defaults
    if( ! (hue & 0x80) ) {
        if( ! (hue & 0x40) ) {
            if( ! (hue & 0x20) ) {
                r = K255 - third;
                g = third;
                b = 0;
            } else {
                r = K171;
                g = K85 + third ;
                b = 0;
            }
        } else {
            if( !  (hue & 0x20) ) {
                uint8_t twothirds = scale8( offset8, ((256 * 2) / 3)); // max=170
                r = K171 - twothirds;
                g = K170 + third;
                b = 0;
            } else {
                r = 0;
                g = K255 - third;
                b = third;
            }
        }
    } else {
        if( ! (hue & 0x40) ) {
            if( ! ( hue & 0x20) ) {
                r = 0;
                uint8_t twothirds = scale8( offset8, ((256 * 2) / 3)); // max=170
                g = K171 - twothirds;
                b = K85  + twothirds;
            } else {
                r = third;
                g = 0;
                b = K255 - third;
            }
        } else {
            if( !  (hue & 0x20)  ) {
                r = K85 + third;
                g = 0;
                b = K171 - third;
            } else {
                r = K170 + third;
                g = 0;
                b = K85 - third;
            }
        }
    }

    if( sat != 255 ) {
        if( sat == 0) {
            r = 255; b = 255; g = 255;
        } else {
            uint8_t desat = 255 - sat;
            desat = scale8_video( desat, desat);
            uint8_t satscale = 255 - desat;
            r = scale8( r, satscale);
            g = scale8( g, satscale);
            b = scale8( b, satscale);
            uint8_t brightness_floor = desat;
            r += brightness_floor;
            g += brightness_floor;
            b += brightness_floor;
        }
    }

    if( val != 255 ) {
        val = scale8_video( val, val);
        if( val == 0 ) {
            r=0; g=0; b=0;
        } else {
            r = scale8( r, val);
            g = scale8( g, val);
            b = scale8( b, val);
        }
    }

    rgb.r = r;
    rgb.g = g;
    rgb.b = b;
}

#endif
