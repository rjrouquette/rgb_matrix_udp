#!/bin/bash

cd "$(dirname "${BASH_SOURCE[0]}")"

git pull
git submodule update --remote --merge

cd rpi-rgb-led-matrix/
make

cd ../udp_receiver/
./build.sh
sudo ./install.sh

echo "[Done]"