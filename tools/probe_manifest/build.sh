#!/bin/bash
# Builds build/probe_manifest/probe_manifest.xex with xsdk, in WSL:
#   ./tools/probe_manifest/build.sh <xsdk checkout>
# Like the library, it has no xsdk of its own: name a checkout (README.md names the commit last tested).
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
if [ $# -ne 1 ] || [ ! -f "$1/cmake/xsdk.cmake" ]; then
	echo "usage: ./tools/probe_manifest/build.sh <xsdk checkout>   (a clone of github.com/xenosworks/xsdk)" >&2
	exit 2
fi
xsdk="$(cd "$1" && pwd)"

make -s -C "$xsdk" build/libxsdk.a
cmake -S "$root/tools/probe_manifest" -B "$root/build/probe_manifest" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$xsdk/cmake/xsdk.cmake"
cmake --build "$root/build/probe_manifest"
echo "Built $root/build/probe_manifest/probe_manifest.xex"
