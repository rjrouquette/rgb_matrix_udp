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
mv udp_receiver ../dist
mv udp_receiver2 ../dist

cd ..
rm -rf build

if [ ! -f dist/udp_receiver ]; then
    echo "Failed to build udp_receiver"
    exit 1
fi

if [ ! -f dist/udp_receiver2 ]; then
    echo "Failed to build udp_receiver2"
    exit 1
fi

echo "Successfully built: udp_receiver udp_receiver2"
exit 0
