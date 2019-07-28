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

cd ..
rm -rf build

if [ ! -f dist/udp_receiver ]; then
    echo "Failed to build udp_receiver"
    exit 1
fi

echo "Successfully built: udp_receiver"
exit 0
