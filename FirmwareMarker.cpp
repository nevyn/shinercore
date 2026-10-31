#include "FirmwareMarker.h"
#include "Wiring.h"

// Defined apart from its reader in setup(), so the reference survives and --gc-sections keeps the section.
__attribute__((section(".rodata_custom_desc")))
const FirmwareMarker firmwareMarker = {"ShinerCore/1", FIRMWARE_BOARD};
