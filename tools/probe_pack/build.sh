#!/bin/bash
# Builds build/probe_pack/probe_pack.xex with xsdk, in WSL:
#   ./tools/probe_pack/build.sh [<xsdk checkout>]
# Uses Avatar Thrift Store's xsdk submodule unless another checkout is named, until this project has its own.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
xsdk="${1:-$root/../avatar_thrift_store/third_party/xsdk}"
xsdk="$(cd "$xsdk" && pwd)"

make -s -C "$xsdk" build/libxsdk.a
cmake -S "$root/tools/probe_pack" -B "$root/build/probe_pack" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$xsdk/cmake/xsdk.cmake"
cmake --build "$root/build/probe_pack"
echo "Built $root/build/probe_pack/probe_pack.xex"
