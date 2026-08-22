#!/bin/bash
#
# The Simpsons: Hit & Run -- Dreamcast build
#
#   ./build.sh assets /path/to/SRR2    convert the game data (once, ~20 min)
#   ./build.sh                         build the ELF
#   ./build.sh cdi                     build the ELF and bake SRR2.cdi
#   ./build.sh clean                   drop the build directory
#
set -e

SOURCE_DIR="${PWD}"
BUILD_DIR="${PWD}/dcbuild"
DATA_DIR="${SRR2_DATA:-${PWD}/game-dc}"
CDI_OUT="${SRR2_CDI:-${PWD}/SRR2.cdi}"
PVRTEX="${PVRTEX:-/opt/toolchains/dc/kos/utils/pvrtex/pvrtex}"
SPLIT_VERTS="${SPLIT_VERTS:-256}"

if [ -z "${KOS_BASE}" ]; then
    echo "KallistiOS environment not loaded."
    echo "  source /opt/toolchains/dc/kos/environ.sh"
    exit 1
fi

case "$1" in
assets)
    SRC="$2"
    if [ -z "${SRC}" ] || [ ! -d "${SRC}/art" ]; then
        echo "usage: $0 assets /path/to/SRR2   (the folder holding art/, scripts/, sound/)"
        exit 1
    fi
    if [ ! -x "${PVRTEX}" ]; then
        echo "pvrtex not found at ${PVRTEX}; set PVRTEX=/path/to/pvrtex"
        exit 1
    fi

    # Textures are cached by content, so a re-run only redoes the meshes.
    mkdir -p "${DATA_DIR}"
    python3 tools/p3dconv/p3dconv.py "${SRC}/art" "${DATA_DIR}/art" \
        --split-verts "${SPLIT_VERTS}" --pvrtex "${PVRTEX}"

    # Scripts and sound ship as-is.
    cp -r "${SRC}/scripts" "${DATA_DIR}/"
    cp -r "${SRC}/sound"   "${DATA_DIR}/"
    echo "Data ready in ${DATA_DIR}"
    ;;

clean)
    rm -rf "${BUILD_DIR}"
    echo "Removed ${BUILD_DIR}"
    ;;

*)
    # Only reconfigure when there is no cache: rewriting the defines
    # invalidates every object and turns a one-file edit into a full rebuild.
    if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        kos-cmake -G "Unix Makefiles" \
            -DCMAKE_SYSTEM_NAME=Dreamcast \
            -DDREAMCAST=ON \
            -DCMAKE_BUILD_TYPE=Release \
            -S "${SOURCE_DIR}" -B "${BUILD_DIR}"
    fi

    make -C "${BUILD_DIR}" -j"$(nproc)"

    if [ "$1" = "cdi" ]; then
        if [ ! -d "${DATA_DIR}/art" ]; then
            echo "No converted data in ${DATA_DIR}. Run: $0 assets /path/to/SRR2"
            exit 1
        fi
        mkdcdisc -e "${BUILD_DIR}/SRR2.elf" -D "${DATA_DIR}" -o "${CDI_OUT}"
        echo "Wrote ${CDI_OUT}"
    fi
    ;;
esac
