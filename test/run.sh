#!/bin/sh
# Host-side tests: pure logic only (codecs, migration, wiring), Arduino surface stubbed.
# They build as an Atom Lite; wiringtest passes every board explicitly.
set -e
cd "$(dirname "$0")"
mkdir -p build
HOST="-I stubs -I $HOME/Dev/Arduino/libraries/ArduinoJson/src -DARDUINO_M5STACK_ATOM"

clang++ -std=c++17 $HOST -o build/codectest codectest.cpp ../ShinyTypes.cpp
clang++ -std=c++17 -o build/migrationtest migrationtest.cpp
clang++ -std=c++17 $HOST -o build/meshtest meshtest.cpp
clang++ -std=c++17 $HOST -o build/proptest proptest.cpp ../ShinyTypes.cpp
# C++11 like the ESP32 toolchain, so device-only syntax errors show up here first
clang++ -std=c++11 $HOST -o build/wiringtest wiringtest.cpp ../Wiring.cpp ../ShinyTypes.cpp

./build/codectest
./build/migrationtest
./build/meshtest
./build/proptest
./build/wiringtest
