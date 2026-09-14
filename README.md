# INDI driver for the GastroSlew mount

### Installing the INDI driver
```sh
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
sudo make install
```