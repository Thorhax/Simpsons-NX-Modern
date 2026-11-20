#!/bin/bash
set -e

# parallel compile jobs; a full -O3 rebuild with one job per core can run out of memory
JOBS="${JOBS:-4}"
# the devkitPro container gets at most 22GB of RAM and no swap
# (--memory-swap equal to --memory); a runaway compiler is killed inside the
# container instead of pushing the whole machine out of memory

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

docker run --rm --memory=22g --memory-swap=22g -v /root/tmp/switch-upd:/root/tmp/switch-upd -w "${SCRIPT_DIR}/build/projects/sdl/mame/gmake-linux" devkitpro/devkita64:latest bash -c "
set -e
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:\$PATH
MAKEARGS=(config=release CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++ AR=aarch64-none-elf-ar ARCH=\"-O3 -fno-strict-aliasing -fomit-frame-pointer -D__SWITCH__ -DSDLMAME_NO64BITIO -I/opt/devkitpro/portlibs/switch/include -isystem /opt/devkitpro/libnx/include -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE\")
# several sub-projects share obj/Release/emu.h.gch; in a parallel clean build
# they all precompile it at once and read each other's half-written file
# (\"cannot read PCH file\"), so build it alone first (a no-op when current)
make -f emu.make \"\${MAKEARGS[@]}\" obj/Release/emu.h.gch
make \"\${MAKEARGS[@]}\" -j${JOBS:-4} mame
nacptool --create 'The Simpsons' 'Thorhax' '1.2.0' ../../../../../simpsons.nacp
elf2nro ../../../../../mame ../../../../../simpsons.nro --icon=${SCRIPT_DIR}/simpsons-icon.jpg --nacp=../../../../../simpsons.nacp
"

# assemble an SD-card-ready folder next to the source tree
PKG="${SCRIPT_DIR}/../package"
rm -rf "${PKG}"
mkdir -p "${PKG}"
cp -a "${SCRIPT_DIR}/switch-package/switch" "${PKG}/"
find "${PKG}" -name .gitkeep -delete
cp "${SCRIPT_DIR}/simpsons.nro" "${PKG}/switch/simpsons-arcade/simpsons.nro"

echo "Build successful:"
echo " - ${SCRIPT_DIR}/simpsons.nro (boots directly into The Simpsons)"
echo " - ${PKG}/switch/simpsons-arcade/ (copy the 'switch' folder to the SD card root)"
