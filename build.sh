#!/bin/bash
# Builds the library and its samples with xsdk, or the PC tools, in WSL:
#   ./build.sh <xsdk checkout>       build/console: libxsdk_avatar.a and the samples (hello_avatar_world, store,
#                                    rig_export .xex), with that xsdk's toolchain
#   ./build.sh pc <xsdk checkout>    build/pc: avatar_dump and the checks, with the host gcc (xsdk's headers only)
# The library has no xsdk of its own: a title builds it with its own xsdk (add_subdirectory under xsdk's toolchain
# file), and building this repository on its own names a checkout. README.md names the xsdk commit last tested.
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"

usage() {
	echo "usage: ./build.sh <xsdk checkout>       the library and the samples, for the console" >&2
	echo "       ./build.sh pc <xsdk checkout>    the PC tools and checks" >&2
	echo "$1" >&2
	exit 2
}

pc=false
if [ "${1:-}" = pc ]; then
	pc=true
	shift
fi
[ $# -eq 1 ] || usage "Name the xsdk checkout to build against (a clone of github.com/xenosworks/xsdk)."
[ -f "$1/cmake/xsdk.cmake" ] || usage "$1 is not an xsdk checkout: it has no cmake/xsdk.cmake."
xsdk="$(cd "$1" && pwd)"

if $pc; then
	# The asset and animation layers: the files that build for the PC.
	portable=(alloc animation camera description locomotion lzx model pack player shape skeleton stfs strb texture
		own_animations)
	sources=("${portable[@]/#/$root/src/avatar_}")
	sources=("${sources[@]/%/.c}")
	# The avatar layer's loading half and the rig export, with xsdk's headers for their declarations and the console's
	# file reads replaced by the test's.
	outfit=("$root/src/avatar_outfit.c" "$root/src/avatar_export.c" "$root/src/avatar_bake.c" "$root/src/avatar_png.c")
	mkdir -p "$root/build/pc/generated"
	python3 "$root/tools/gen_clips.py" "$root/animations" > "$root/build/pc/generated/avatar_clips.h"
	flags=(-std=c17 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$root/build/pc/generated")
	for tool in avatar_dump player_check walk_check anim_check camera_check locomotion_check; do
		gcc "${flags[@]}" "${sources[@]}" "$root/tests/$tool.c" -lm -o "$root/build/pc/$tool"
	done
	gcc "${flags[@]}" -I"$xsdk/include" "${sources[@]}" "${outfit[@]}" "$root/tests/outfit_check.c" -lm \
		-o "$root/build/pc/outfit_check"
	# The out-of-memory tests, with AddressSanitizer: a leak or a double free in an error path ends them.
	gcc "${flags[@]}" -g -fsanitize=address "${sources[@]}" "$root/tests/alloc_check.c" -lm \
		-o "$root/build/pc/alloc_check"
	gcc "${flags[@]}" -g -fsanitize=address -I"$xsdk/include" "${sources[@]}" "${outfit[@]}" \
		"$root/tests/outfit_check.c" -lm -o "$root/build/pc/outfit_alloc_check"
	echo "Built $root/build/pc: avatar_dump, player_check, walk_check, anim_check, camera_check, locomotion_check," \
		"outfit_check, alloc_check and outfit_alloc_check"
	exit 0
fi

make -s -C "$xsdk" build/libxsdk.a build/host/libxsc.so
cmake -S "$root" -B "$root/build/console" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$xsdk/cmake/xsdk.cmake"
cmake --build "$root/build/console"
echo "Built $root/build/console/samples: hello_avatar_world/hello_avatar_world.xex, store/store.xex," \
	"rig_export/rig_export.xex"
