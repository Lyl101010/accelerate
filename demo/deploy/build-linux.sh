#!/bin/bash

set -e
python3 tools/fix_compat.py

echo "Simplified build script on rk3588"

TARGET_SOC="rk3588"
TARGET_ARCH="aarch64"
BUILD_DEMO_NAME="demo"
BUILD_TYPE="Release"
ENABLE_ASAN="OFF"
DISABLE_RGA="OFF"
DISABLE_LIBJPEG="OFF"
GCC_COMPILER="aarch64-linux-gnu"

export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

BUILD_DEMO_PATH="$(cd "$(dirname "$(realpath -s "$0")")" && pwd)"
ROOT_PWD="${BUILD_DEMO_PATH}"

TARGET_PLATFORM="${TARGET_SOC}_linux_${TARGET_ARCH}"
INSTALL_DIR="${ROOT_PWD}/install/demo_Linux_aarch64"
BUILD_DIR="${ROOT_PWD}/build/build_${BUILD_DEMO_NAME}_${TARGET_PLATFORM}_${BUILD_TYPE}"

rm -rf "${INSTALL_DIR}" "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo "==================================="
echo "Building demo for rk3588 (aarch64)"
echo "Build type: ${BUILD_TYPE}"
echo "Install dir: ${INSTALL_DIR}"
echo "==================================="

cd "${BUILD_DIR}"
cmake "${BUILD_DEMO_PATH}" \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=${TARGET_ARCH} \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DDISABLE_RGA=${DISABLE_RGA} \
    -DDISABLE_LIBJPEG=${DISABLE_LIBJPEG} \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

make -j"$(nproc)"
make install

echo "Build completed! Output in: ${INSTALL_DIR}"
