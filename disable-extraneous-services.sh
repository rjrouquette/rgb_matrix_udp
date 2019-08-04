# disable apt updates
sudo systemctl stop apt-daily.timer
sudo systemctl disable apt-daily.timer
sudo systemctl disable apt-daily.service
sudo systemctl stop apt-daily-upgrade.timer
sudo systemctl disable apt-daily-upgrade.timer
sudo systemctl disable apt-daily-upgrade.service

# disable irqbalance
sudo systemctl stop irqbalance
sudo systemctl disable irqbalance

# disable wpa supplicant
sudo systemctl stop wpa_supplicant
sudo systemctl disable wpa_supplicant
