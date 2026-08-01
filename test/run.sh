#!/bin/sh
# Host-side tests: pure logic only (codecs, migration), Arduino surface stubbed.
set -e
cd "$(dirname "$0")"
mkdir -p build

clang++ -std=c++17 -I stubs -o build/codectest codectest.cpp ../ShinyTypes.cpp
clang++ -std=c++17 -o build/migrationtest migrationtest.cpp
clang++ -std=c++17 -I stubs -o build/meshtest meshtest.cpp

./build/codectest
./build/migrationtest
./build/meshtest
