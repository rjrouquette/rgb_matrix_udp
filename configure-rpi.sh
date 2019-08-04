# configure rpi
echo 'blacklist snd_bcm2835' | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
sudo update-initramfs -u

# fix system time
sudo apt -y install ntpdate
sudo ntpdate 192.168.3.200
sudo apt -y remove ntpdate

# update system packages
sudo apt update
sudo apt -y dist-upgrade

# install dependencies
sudo apt -y install chrony cmake build-essential

# build and install receiver
git clone --recursive https://github.com/rjrouquette/rgb_matrix_udp.git
cd rgb_matrix_udp/rpi-rgb-led-matrix
export USER_DEFINES=-DFIXED_FRAME_MICROSECONDS=5000
make
cd ../udp_receiver
./build.sh
sudo ./install.sh
cd ../..
