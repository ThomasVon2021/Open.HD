# bin/bash

# convenient script to build this project with cmake

rm -rf build

mkdir build

cd build

#cmake -G Ninja ..
#ninja

cmake ..
make -j$(nproc)
