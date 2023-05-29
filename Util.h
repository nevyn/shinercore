class Logger : public Print
{
    virtual size_t write(uint8_t c)
    {
        Serial.write(c);
        if(M5.getDisplayCount() > 0)
        {
            M5GFX &display = M5.getDisplay(0);
            return display.write(c);
        }
    }
};
Logger logger;

float frand(void)
{
  return random(1000)/1000.0;
}

// Over the input range of 0.0f->1.0f, returns a single sinusoidal cycle
// starting at 0.0f, with the peak at 1.0f.
float curve(float progress)
{
    return sin((progress-0.25)*6.28f)/2.0f + 0.5f;
}

CRGB rgbFromString(const String &str)
{
    int firstSpace = str.indexOf(' ');
    int secondSpace = str.lastIndexOf(' ');
    return CRGB(
        str.substring(0, firstSpace).toInt(), 
        str.substring(firstSpace+1, secondSpace).toInt(),
        str.substring(secondSpace+1, str.length()).toInt()
    );
}