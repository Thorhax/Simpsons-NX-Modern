#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

docker run --rm -v /root/tmp/switch-upd:/root/tmp/switch-upd -w "${SCRIPT_DIR}/build/projects/sdl/mame/gmake-linux" devkitpro-mesa-rust:latest bash -c "
set -e
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:\$PATH
make config=release CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++ AR=aarch64-none-elf-ar ARCH=\"-O3 -fno-strict-aliasing -fomit-frame-pointer -D__SWITCH__ -DSDLMAME_NO64BITIO -I/opt/devkitpro/portlibs/switch/include -isystem /opt/devkitpro/libnx/include -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE\" -j\$(nproc) mame
nacptool --create 'The Simpsons' 'Thorhax' '1.0.0' ../../../../../simpsons.nacp
elf2nro ../../../../../mame ../../../../../simpsons.nro --icon=${SCRIPT_DIR}/simpsons-icon.jpg --nacp=../../../../../simpsons.nacp
"

# assemble an SD-card-ready folder next to the source tree
PKG="${SCRIPT_DIR}/../package"
rm -rf "${PKG}"
mkdir -p "${PKG}"
cp -a "${SCRIPT_DIR}/switch-package/switch" "${PKG}/"
find "${PKG}" -name .gitkeep -delete
cp "${SCRIPT_DIR}/simpsons.nro" "${PKG}/switch/simpsons/simpsons.nro"

echo "Build successful:"
echo " - ${SCRIPT_DIR}/simpsons.nro (boots directly into The Simpsons)"
echo " - ${PKG}/switch/simpsons/ (copy the 'switch' folder to the SD card root)"
