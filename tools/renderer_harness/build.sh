#!/usr/bin/env bash
#
# Build and run the smallest harness that proves the M2 integration boundary.
#
# The harness compiles two upstream translation units from an external
# SAT-R/sa2 checkout, archives them, and links a host from this repository
# against that archive. Upstream is never copied into this repository and never
# modified. The host calls the upstream software PPU and checks its output.
#
# Usage:
#   SA2_UPSTREAM=/path/to/sa2 tools/renderer_harness/build.sh
#
# Overrides (only for deliberate experiments; the default is fail-closed):
#   SAR_BUILD_ROOT=/path       build directory (default: <repo>/build)
#   SAR_ALLOW_DIRTY_UPSTREAM=1 allow tracked modifications in the checkout
#   SAR_ALLOW_OTHER_REV=1      allow a checkout at a revision other than the pin
#   CC, CXX, AR                compiler and archiver

set -euo pipefail

die() {
    printf 'renderer-harness: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/../.." && pwd -P)
pin_file="$repo_root/tools/upstream.pin"

[ -f "$pin_file" ] || die "missing $pin_file"
pin=$(awk 'NF && $1 !~ /^#/ { print $1; exit }' "$pin_file")
[ -n "$pin" ] || die "no commit in $pin_file"

upstream=${SA2_UPSTREAM:-}
[ -n "$upstream" ] || die "set SA2_UPSTREAM to the external SAT-R/sa2 checkout"
[ -d "$upstream" ] || die "SA2_UPSTREAM is not a directory: $upstream"

upstream_real=$(cd "$upstream" && pwd -P) || die "cannot resolve SA2_UPSTREAM"
case "$upstream_real/" in
    "$repo_root/"*) die "SA2_UPSTREAM resolves inside this repository: $upstream_real" ;;
esac

git -C "$upstream_real" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "SA2_UPSTREAM is not a git checkout"
head=$(git -C "$upstream_real" rev-parse HEAD)

dirty=$(git -C "$upstream_real" status --porcelain --untracked-files=no)
if [ -n "$dirty" ] && [ "${SAR_ALLOW_DIRTY_UPSTREAM:-0}" != "1" ]; then
    printf '%s\n' "$dirty" >&2
    die "SA2_UPSTREAM has tracked modifications; commit, clean or set SAR_ALLOW_DIRTY_UPSTREAM=1"
fi

if [ "$head" != "$pin" ] && [ "${SAR_ALLOW_OTHER_REV:-0}" != "1" ]; then
    die "SA2_UPSTREAM is at $head, expected $pin (set SAR_ALLOW_OTHER_REV=1 to override)"
fi

for source in \
    "$upstream_real/src/platform/shared/common.c" \
    "$upstream_real/src/platform/shared/video/gpsp_renderer.cc" \
    "$upstream_real/include/global.h"; do
    [ -f "$source" ] || die "upstream file missing: $source"
done

CC=${CC:-cc}
CXX=${CXX:-c++}
AR=${AR:-ar}
for tool in "$CC" "$CXX" "$AR"; do
    command -v "$tool" >/dev/null 2>&1 || die "tool not found: $tool"
done

build_root=${SAR_BUILD_ROOT:-"$repo_root/build"}
case "$build_root" in
    /*) ;;
    *) build_root="$repo_root/$build_root" ;;
esac
build_root=$(realpath -m "$build_root") || die "cannot resolve SAR_BUILD_ROOT"
case "$build_root/" in
    "$upstream_real/"*) die "SAR_BUILD_ROOT must not be inside the upstream checkout: $build_root" ;;
esac
build_dir="$build_root/renderer_harness"
mkdir -p "$build_dir"

common_flags=(
    -I"$upstream_real/include"
    -DGAME=GAME_SA2
    -DUSA
    -DPLATFORM_GBA=0
    -DPLATFORM_SDL=1
    -DPLATFORM_WIN32=0
    -DCPU_ARCH_X86=0
    -DCPU_ARCH_ARM=0
    -DPORTABLE=1
    -DDEBUG=0
    -DTAS_TESTING=0
    -DENABLE_DECOMP_CREDITS=0
    -DRENDERER=RENDERER_SOFTWARE
    -O2
)
c_flags=("${common_flags[@]}" -Wimplicit -Wparentheses -Werror -Wno-unused-value)
cxx_flags=("${common_flags[@]}" -Werror -Wno-parentheses-equality -Wno-unused-value -fno-rtti -fno-exceptions -std=c++11)

status_before=$(git -C "$upstream_real" status --porcelain --untracked-files=no)

printf 'renderer-harness: upstream %s at %s\n' "$upstream_real" "$head"
printf 'renderer-harness: compiling upstream common.c\n'
"$CC" "${c_flags[@]}" -c "$upstream_real/src/platform/shared/common.c" -o "$build_dir/common.o"
printf 'renderer-harness: compiling upstream gpsp_renderer.cc\n'
"$CXX" "${cxx_flags[@]}" -c "$upstream_real/src/platform/shared/video/gpsp_renderer.cc" -o "$build_dir/gpsp_renderer.o"
printf 'renderer-harness: compiling host probe.c\n'
"$CC" "${c_flags[@]}" -c "$script_dir/probe.c" -o "$build_dir/probe.o"

printf 'renderer-harness: archiving the upstream core\n'
rm -f "$build_dir/libsa2core_min.a"
"$AR" rcs "$build_dir/libsa2core_min.a" "$build_dir/common.o" "$build_dir/gpsp_renderer.o"

printf 'renderer-harness: linking the host against the archive\n'
"$CC" "$build_dir/probe.o" "$build_dir/libsa2core_min.a" -Wl,--no-undefined -lm -o "$build_dir/probe"

printf 'renderer-harness: running the host\n'
if ! "$build_dir/probe" >"$build_dir/harness.log" 2>&1; then
    cat "$build_dir/harness.log" >&2
    die "probe failed"
fi
cat "$build_dir/harness.log"

grep -q '^harness: upstream software PPU 426x240$' "$build_dir/harness.log" \
    || die "unexpected PPU dimensions"
grep -q '^harness: upstream interrupt dispatch ok$' "$build_dir/harness.log" \
    || die "upstream interrupt dispatch failed"
grep -Eq '^harness: forced-blank checksum [0-9a-f]{8}$' "$build_dir/harness.log" \
    || die "missing checksum"
if [ "$head" = "$pin" ]; then
    grep -q '^harness: forced-blank checksum 5873dfc0$' "$build_dir/harness.log" \
        || die "pinned upstream produced an unexpected checksum"
fi

status_after=$(git -C "$upstream_real" status --porcelain --untracked-files=no)
[ "$status_before" = "$status_after" ] || die "the harness changed tracked files in the upstream checkout"

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{ print $1 }'
    else
        shasum -a 256 "$1" | awk '{ print $1 }'
    fi
}

{
    printf 'upstream=%s\n' "$upstream_real"
    printf 'commit=%s\n' "$head"
    printf 'pin=%s\n' "$pin"
    printf 'common.c=%s\n' "$(hash_file "$upstream_real/src/platform/shared/common.c")"
    printf 'gpsp_renderer.cc=%s\n' "$(hash_file "$upstream_real/src/platform/shared/video/gpsp_renderer.cc")"
    printf 'probe.c=%s\n' "$(hash_file "$script_dir/probe.c")"
} >"$build_dir/provenance.txt"

printf 'renderer-harness: ok; artifacts in %s\n' "$build_dir"
