#!/bin/bash
# Define the source directory and build directory
SOURCE_DIR="${PWD}"
BUILD_DIR="${PWD}/dcbuild"

BUILD_JOBS=$(nproc) # Use all available CPU cores by default
# Run CMake to configure the project with the selected options
kos-cmake -G "Unix Makefiles" \
      -DCMAKE_SYSTEM_NAME=Dreamcast \
	  -DDREAMCAST=ON \
      -S "$SOURCE_DIR" \
	  -DCMAKE_BUILD_TYPE=Release \
	  -B "$BUILD_DIR"
# Build the project
cd dcbuild && make -j"$BUILD_JOBS"

# Print a message indicating the build is complete
echo "Dreamcast build complete!"

# Parse command line arguments
BUILD_CDI=false
RUN_EMU=false

for arg in "$@"; do
    case $arg in
        --cdi)
            BUILD_CDI=true
            ;;
        --emu)
            RUN_EMU=true
            ;;
    esac
done

# Build CDI if requested
if [ "$BUILD_CDI" = true ] || [ "$RUN_EMU" = true ]; then
    cd .. && mkdcdisc -e dcbuild/SRR2.elf -D ./game -o ./SRR2.cdi
    echo "Dreamcast CDI complete!"
fi

# Run emulator if requested
if [ "$RUN_EMU" = true ]; then
    echo "Launching Flycast emulator..."
    ../flycast-x86_64.AppImage ./SRR2.cdi
fi