#!/bin/bash
# Builds the library and its samples with xsdk, or the PC tools, in WSL:
#   ./build.sh [<xsdk checkout>]     build/console: libxsdk_avatar.a and the samples (viewer, hello, store, rig_export .xex)
#   ./build.sh pc [<xsdk checkout>]  build/pc: avatar_dump and the checks, with the host gcc
# Uses Avatar Thrift Store's xsdk submodule unless another checkout is named, until this project has its own.
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"

if [ "${1:-}" = pc ]; then
	xsdk="${2:-$root/../avatar_thrift_store/third_party/xsdk}"
	# The asset and animation layers: the files that build for the PC.
	portable=(alloc animation description lzx model pack player shape skeleton stfs strb texture own_animations)
	sources=("${portable[@]/#/$root/src/avatar_}")
	sources=("${sources[@]/%/.c}")
	mkdir -p "$root/build/pc/generated"
	python3 "$root/tools/gen_clips.py" "$root/animations" > "$root/build/pc/generated/avatar_clips.h"
	flags=(-std=c17 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$root/build/pc/generated")
	for tool in avatar_dump player_check walk_check anim_check; do
		gcc "${flags[@]}" "${sources[@]}" "$root/tests/$tool.c" -lm -o "$root/build/pc/$tool"
	done
	# The avatar layer's loading half and the rig export too, with xsdk's headers for their declarations and the
	# console's file reads replaced by the test's.
	gcc "${flags[@]}" -I"$xsdk/include" "${sources[@]}" "$root/src/avatar_outfit.c" "$root/src/avatar_export.c" \
		"$root/src/avatar_bake.c" "$root/src/avatar_png.c" "$root/tests/outfit_check.c" -lm \
		-o "$root/build/pc/outfit_check"
	echo "Built $root/build/pc/avatar_dump, player_check, walk_check, anim_check and outfit_check"
	exit 0
fi

xsdk="${1:-$root/../avatar_thrift_store/third_party/xsdk}"
xsdk="$(cd "$xsdk" && pwd)"
make -s -C "$xsdk" build/libxsdk.a build/host/libxsc.so
cmake -S "$root" -B "$root/build/console" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$xsdk/cmake/xsdk.cmake"
cmake --build "$root/build/console"
echo "Built $root/build/console/samples: viewer/viewer.xex, hello/hello.xex, store/store.xex, rig_export/rig_export.xex"
