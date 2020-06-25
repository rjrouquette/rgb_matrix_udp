#!/bin/bash

cd "$(dirname "${BASH_SOURCE[0]}")"

mkdir -p dist/
mkdir -p build/
rm dist/*
cd build

CORES=$(nproc)
echo "Running build with $CORES threads."

rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release ..
#make VERBOSE=1
make -j$CORES
mv udp_transmitter ../dist
mv testing ../dist

cd ..
rm -rf build

if [ ! -f dist/udp_transmitter ]; then
    echo "Failed to build udp_transmitter"
    exit 1
fi

echo "Successfully built: udp_transmitter"
exit 0
