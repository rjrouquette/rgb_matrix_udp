#!/usr/bin/env bash

cp --remove-destination -v dist/udp_receiver /usr/local/bin/rgb_matrix_udp_receiver
cp --remove-destination -v ../rpi-rgb-led-matrix/lib/librgbmatrix.so.1 /usr/local/lib/
cp --remove-destination -v rgb-matrix-udp-receiver.service /etc/systemd/system/

systemctl daemon-reload
systemctl restart rgb-matrix-udp-receiver.service
systemctl status rgb-matrix-udp-receiver.service
