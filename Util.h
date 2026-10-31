#ifndef UTIL__H
#define UTIL__H
#include "M5Unified.h"
#include "FastLED.h"

class Logger : public Print
{
    virtual size_t write(uint8_t c)
    {
        size_t written = Serial.write(c);
        if(M5.getDisplayCount() > 0)
        {
            M5GFX &display = M5.getDisplay(0);
            return display.write(c);
        }
        return written;
    }
};
extern Logger logger;


inline float frand(void)
{
  return random(1000)/1000.0;
}

#endif