# Building and testing

## Board variants

All builds need `--libraries ~/Dev/Arduino/libraries` if the default sketchbook isn't
configured (OverAnimate lives there, unpublished).

Two libraries carry fixes that the firmware needs:

* ArduinoBLE: the github.com/nevyn/ArduinoBLE fork, branch `shinercore`
  ([ble-comms.md](ble-comms.md)).
* FastLED: the github.com/nevyn/FastLED fork, branch `shinercore` = upstream tag
  3.6.0 plus the RMT start-race fix ([wiring.md](wiring.md), LED pins). A patched
  build's map file contains `gStartMux`.

On a new build machine, replace the library-manager copies:

```
git clone -b shinercore git@github.com:nevyn/ArduinoBLE.git ~/Dev/Arduino/libraries/ArduinoBLE
git clone -b shinercore git@github.com:nevyn/FastLED.git ~/Dev/Arduino/libraries/FastLED
```

```
# M5AtomS3
arduino-cli compile -b m5stack:esp32:m5stack_atoms3

# M5Atom Lite/Matrix
arduino-cli compile -b m5stack:esp32:m5stack_atom

# M5Atom Echo: same board + define. No core defines ARDUINO_M5STACK_ATOM_ECHO;
# this flag is what makes an Echo an Echo (mic on by default, no preset pins).
arduino-cli compile -b m5stack:esp32:m5stack_atom --build-property "compiler.cpp.extra_flags=-DARDUINO_M5STACK_ATOM_ECHO"
```

Compile all three before claiming a change builds. `upload` uses the latest build in
the cache — recompile the variant you're flashing right before `arduino-cli upload`.

## Host tests

`test/run.sh` compiles and runs the host-side tests (codecs, migration, properties,
wiring) with clang, against the ArduinoJson in `~/Dev/Arduino/libraries`.
They stub the Arduino surface via `test/stubs/FastLED.h`; pure logic only, no BLE or
LED code. Keep them passing; extend them when you touch what they cover.

## Flashing and serial

* Atom-family boards with a CH552 bridge enumerate as `/dev/cu.usbserial-*`; AtomS3
  (native USB) as `/dev/cu.usbmodem*`.
* "Port doesn't exist" while `ls` shows it: something holds the port — `lsof` it.
  The Arduino IDE's serial-monitor helper process is the usual culprit; kill the
  helper PID (not the IDE).
* `arduino-cli monitor` exits immediately without a TTY, and `stty`+`head` wedges the
  DTR/RTS lines (holds the chip in reset). What works headless:

```
screen -dmS shinerserial /dev/cu.usbserial-XXX 115200
screen -S shinerserial -p 0 -X logfile /tmp/serial.log
screen -S shinerserial -p 0 -X log on
# ... do things ...
screen -S shinerserial -p 0 -X log off
screen -S shinerserial -X quit        # releases the port for flashing
```

`-X hardcopy` only dumps one screenful; use logging for anything longitudinal.

## Building from a worktree

arduino-cli requires the sketch directory to be named `shinercore`, which a worktree
isn't. A directory symlink gets resolved and rejected; symlink the *files* instead:

```
mkdir -p /tmp/meshbuild/shinercore
for f in <worktree>/*.ino <worktree>/*.h <worktree>/*.cpp; do ln -s "$f" /tmp/meshbuild/shinercore/; done
arduino-cli compile -b ... /tmp/meshbuild/shinercore
```

Re-run the link loop after adding a new source file.

## Debugging a hang: the core dump

A task watchdog reboot prints a useless `|<-CORRUPTED` backtrace from the watchdog
interrupt. The real state is in flash: the core's prebuilt IDF saves an ELF core dump of
every task to the `coredump` partition (0x3F0000, 64 KB) on any panic, without logging
it. Flashing an app doesn't erase the dump; the next panic overwrites it. Decoding the
dump needs the exact ELF that was running. Its SHA prefix is the panic's
`ELF file SHA256` line, so copy `<build-path>/shinercore.ino.elf` out of the build
directory before the next compile overwrites it.

```
T=~/Library/Arduino15/packages/m5stack/tools   # Atom and Echo (ESP32)
$T/esptool_py/4.5.1/esptool --chip esp32 --port /dev/cu.usbserial-XXX --baud 115200 \
    read_flash 0x3F0000 0x10000 part.bin   # 460800 fails on the CH552 bridge
python3 -c "import struct,sys; b=open('part.bin','rb').read(); n=struct.unpack('<I',b[:4])[0]; open('core.elf','wb').write(b[20:n-4])"
$T/xtensa-esp-elf-gdb/11.2_20220823/bin/xtensa-esp32-elf-gdb -batch \
    -ex "thread apply all bt" shinercore.ino.elf core.elf
```

The `ESP_CORE_DUMP_INFO` note in `core.elf` holds the app SHA it came from. The dump
holds task stacks and TCBs, not globals: locals in the blocked frames are what you get.

## Hands-free audio testing

The beat detector is tested by generating WAV test signals (python stdlib `wave`:
kick sweeps, pads, hats at known bpm) and playing them near the device with
`afplay -v 0.3..1.0`, while capturing the serial debug meter. Count ONSET/GRID/RETEMPO
lines in the log against the track's ground truth. See docs/beat-detection.md for the
standard battery and expected results. Generation scripts are throwaway; the method is
the artifact.
