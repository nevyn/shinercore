# Shinercore

ShinerCore is an open source firmware/Arduino sketch for ESP32 controllers
to make pretty animations on addressable RGB LED strips (such as NeoPixel strips).
It's hard-coded to work well with M5Stack's M5AtomS3 modules, which are very small
and cheap and work really great for the use case, but it shouldn't be too much of
a pain to port it to another ESP32 platform.

The current feature set includes the ability to configure the animations over
Bluetooth with [an iOS app](https://github.com/nevyn/ShinerCoreRemote). The goal
is to at beat syncing (having the animations blink to the beat of nearby music
using a microphone peripheral); and to create a mesh network over bluetooth
to sync animations with your friends, so you all pulse in time and with each
others' colors.

## todo

- [ ] connect to every other shinercore in range
- [ ] incorporate every other shinercore's primary color in the main animation
- [ ] sync time between shinercores using a random master
- [ ] if m5stick, use audio to sync timing to beat instead of using raw clock
- [ ] if m5stick, automatically take master clock in mesh


## Resources
https://btprodspecificationrefs.blob.core.windows.net/assigned-numbers/Assigned%20Number%20Types/Assigned_Numbers.pdf
