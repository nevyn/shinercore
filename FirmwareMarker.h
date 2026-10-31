#ifndef __FIRMWARE_MARKER__H
#define __FIRMWARE_MARKER__H

// Tells the flasher that a core runs ShinerCore, for which board, and when it was built, even
// for a local build whose fingerprint no firmware build lists. The linker places it right
// after esp_app_desc_t, at flash 0x10120 (docs/firmware-builds.md).
struct FirmwareMarker
{
    char magic[16]; // "ShinerCore/1"; the number is the layout version
    char board[16]; // the firmware build's board id
    char built[24]; // __DATE__ " " __TIME__, the compiling machine's local time
};

extern const FirmwareMarker firmwareMarker;

#endif
