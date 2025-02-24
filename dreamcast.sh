# Based on SDL2 dreamcast port build script by GPF

#!/bin/bash
# Define the path to the Dreamcast toolchain file
#export KOS_CMAKE_TOOLCHAIN="/opt/toolchains/dc/kos/utils/cmake/dreamcast.toolchain.cmake"
# Define the source directory and build directory
SOURCE_DIR="${PWD}"
BUILD_DIR="${PWD}/dcbuild"

SDL2_DIR="/opt/toolchains/dc/kos/addons/lib/dreamcast/cmake/SDL2"
BUILD_JOBS=$(nproc) # Use all available CPU cores by default
# Run CMake to configure the project with the selected options
cmake -DCMAKE_TOOLCHAIN_FILE="$KOS_CMAKE_TOOLCHAIN" \
      -G "Unix Makefiles" \
      -DCMAKE_SYSTEM_NAME=Dreamcast \
	  -DDREAMCAST=ON \
      -S "$SOURCE_DIR" \
	  -DCMAKE_BUILD_TYPE=Debug \
	  -DSDL2_DIR="$SDL2_DIR" \
	  -B "$BUILD_DIR"
# Build the project
cd dcbuild && make -j"$BUILD_JOBS"

# Print a message indicating the build is complete
echo "Dreamcast build complete!"
cd .. && mkdcdisc/mkdcdisc -e dcbuild/SRR2.elf -D dcbuild/game -o dcbuild/SRR2.cdi

echo "Dreamcast CDI complete!"