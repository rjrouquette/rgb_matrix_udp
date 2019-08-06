#!/usr/bin/env bash

cp --remove-destination -v dist/udp_transmitter /usr/local/bin/rgb_matrix
cp --remove-destination -v rgb-matrix.service /etc/systemd/system/
cp --remove-destination -v config.json /etc/rgb-matrix.json

systemctl daemon-reload
systemctl enable rgb-matrix.service
systemctl restart rgb-matrix.service
systemctl status rgb-matrix.service
