#!/bin/bash
# -----------------------------------------------------------------------------
# This script automates the process of setting up a cross-compilation
# environment for the fdk-aac library using CMake. It prepares the build
# directory, sets the toolchain for cross-compilation, clones the fdk-aac
# repository if not present, configures the build using CMake, compiles the
# library, and finally installs the built library and relevant headers.
# -----------------------------------------------------------------------------

set -e
set -o pipefail

# Variables
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../3rdparty"
FDK_AAC_REPO="https://github.com/nschimme/fdk-aac"
FDK_AAC_DIR="${BUILD_DIR}/fdk-aac"
FDK_AAC_VER="tinification"  # Update to the desired version or commit hash
MAKEFILE="$SCRIPT_DIR/../Makefile"

PRUDYNT_CROSS="${PRUDYNT_CROSS#ccache }"

CC="${PRUDYNT_CROSS}gcc"
CXX="${PRUDYNT_CROSS}g++"
STRIP="${PRUDYNT_CROSS}strip --strip-unneeded"

# Create fdk-aac build directory
echo "Creating fdk-aac build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Clone fdk-aac if not already present
if [ ! -d "$FDK_AAC_DIR" ]; then
    echo "Cloning fdk-aac..."
    git clone "$FDK_AAC_REPO"
fi

cd "$FDK_AAC_DIR"

# Checkout desired version
if [[ -n "$FDK_AAC_VER" ]]; then
    git fetch origin
    git checkout $FDK_AAC_VER
else
    echo "Pulling fdk-aac master"
fi

# Create and navigate to build directory
mkdir -p build
cd build

# Configure the fdk-aac build with CMake
echo "Configuring fdk-aac library..."
cmake \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=mipsle \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-Os -ffunction-sections -fdata-sections" \
    -DCMAKE_CXX_FLAGS="-Os -ffunction-sections -fdata-sections" \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=TRUE \
    -DCMAKE_POLICY_DEFAULT_CMP0069=NEW \
    -DDISABLE_SBR_ENCODER=ON \
    -DDISABLE_SAC_ENCODER=ON \
    -DDISABLE_META_ENCODER=ON \
    -DDISABLE_NOISE_SHAPING=ON \
    -DDISABLE_TRANSPORT_ENCODER=ON \
    -DDISABLE_STEREO=ON \
    -DDISABLE_DECODERS=ON \
    ..

echo "Building fdk-aac library..."
make -j$(nproc)

# Install fdk-aac library and headers
echo "Installing fdk-aac library and headers..."
make install

echo "Stripping fdk-aac library..."
#$STRIP "${BUILD_DIR}/install/lib/libfdk-aac.a"

echo "fdk-aac build complete!"

