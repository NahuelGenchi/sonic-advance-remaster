#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# M2 stage 0 build driver.
#
# Validates a pinned, clean, external SAT-R/sa2 checkout, snapshots it outside
# this repository, builds the upstream core objects, archives everything except
# the upstream SDL platform object, then compiles and links the project-owned
# host from remaster/host/.
#
# Usage:
#   SA2_UPSTREAM=/path/to/sa2 tools/stage0/build.sh
#
# Environment:
#   SA2_UPSTREAM    Required. External SAT-R/sa2 checkout at tools/upstream.pin.
#   SA2_ROM         Optional. Sonic Advance 2 (USA) ROM; validated by SHA-1 and
#                   copied into the disposable snapshot as baserom_sa2.gba.
#   SAR_BUILD_ROOT  Build directory. Default: ${TMPDIR:-/tmp}/sar-stage0.
#   SAR_TAS_TESTING 1 (default) builds the recorded tape; 0 builds interactive.
#   CC, AR, MAKE    Tool overrides.

set -euo pipefail

die() {
    printf 'stage0: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/../.." && pwd -P)
pin_file="$repo_root/tools/upstream.pin"
rom_sha1_expected="7bcd6a07af7c894746fa28073fe0c0e34408022d"

[ -f "$pin_file" ] || die "missing $pin_file"
pin=$(awk 'NF && $1 !~ /^#/ { print $1; exit }' "$pin_file")
[ -n "$pin" ] || die "no commit in $pin_file"

upstream=${SA2_UPSTREAM:-}
[ -n "$upstream" ] || die "set SA2_UPSTREAM to the external SAT-R/sa2 checkout"
[ -d "$upstream" ] || die "SA2_UPSTREAM is not a directory: $upstream"
upstream_real=$(cd "$upstream" && pwd -P) || die "cannot resolve SA2_UPSTREAM"

repo_real=$(cd "$repo_root" && pwd -P)
case "$upstream_real/" in
    "$repo_real/"*) die "SA2_UPSTREAM resolves inside this repository: $upstream_real" ;;
esac

# A linked worktree of this repository is still this repository. Refuse any
# path under any worktree, not only under the active one.
while IFS= read -r line; do
    case "$line" in
        worktree\ *)
            worktree_path=${line#worktree }
            worktree_real=$(cd "$worktree_path" 2>/dev/null && pwd -P) || continue
            case "$upstream_real/" in
                "$worktree_real/"*) die "SA2_UPSTREAM resolves inside this repository worktree: $upstream_real" ;;
            esac
            ;;
    esac
done < <(git -C "$repo_root" worktree list --porcelain)

git -C "$upstream_real" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "SA2_UPSTREAM is not a git checkout"
head=$(git -C "$upstream_real" rev-parse HEAD)
[ "$head" = "$pin" ] || die "SA2_UPSTREAM is at $head, expected $pin"

status_before=$(git -C "$upstream_real" status --porcelain --untracked-files=no)
[ -z "$status_before" ] || {
    printf '%s\n' "$status_before" >&2
    die "SA2_UPSTREAM has tracked modifications"
}

for source in \
    "$upstream_real/Makefile" \
    "$upstream_real/src/main.c" \
    "$upstream_real/src/platform/pret_sdl/sdl2.c" \
    "$upstream_real/include/global.h" \
    "$upstream_real/data/sa2/recordings/dashjump_any_percent.bin"; do
    [ -f "$source" ] || die "upstream file missing: $source"
done

tas=${SAR_TAS_TESTING:-1}
case "$tas" in
    0 | 1) ;;
    *) die "SAR_TAS_TESTING must be 0 or 1" ;;
esac

build_root=${SAR_BUILD_ROOT:-"${TMPDIR:-/tmp}/sar-stage0"}
case "$build_root" in
    /*) ;;
    *) build_root="$repo_root/$build_root" ;;
esac
build_root=$(realpath -m "$build_root") || die "cannot resolve SAR_BUILD_ROOT"
[ "$build_root" != "/" ] || die "SAR_BUILD_ROOT must not be /"
case "$build_root/" in
    "$upstream_real/"*) die "SAR_BUILD_ROOT must not be inside the upstream checkout: $build_root" ;;
esac
while IFS= read -r line; do
    case "$line" in
        worktree\ *)
            worktree_path=${line#worktree }
            worktree_real=$(cd "$worktree_path" 2>/dev/null && pwd -P) || continue
            case "$build_root/" in
                "$worktree_real/"*) die "SAR_BUILD_ROOT must be outside this repository: $build_root" ;;
            esac
            ;;
    esac
done < <(git -C "$repo_root" worktree list --porcelain)

CC=${CC:-cc}
AR=${AR:-ar}
MAKE=${MAKE:-make}
for tool in "$CC" "$AR" "$MAKE" sdl2-config tar; do
    command -v "$tool" >/dev/null 2>&1 || die "tool not found: $tool"
done

rom=${SA2_ROM:-}
rom_sha1=
if [ -n "$rom" ]; then
    [ -f "$rom" ] || die "SA2_ROM is not a file: $rom"
    rom_sha1=$(sha1sum "$rom" | awk '{ print $1 }')
    [ "$rom_sha1" = "$rom_sha1_expected" ] \
        || die "SA2_ROM has SHA-1 $rom_sha1, expected $rom_sha1_expected"
fi

snapshot="$build_root/source"
mkdir -p "$build_root"
rm -rf "$snapshot"
mkdir -p "$snapshot"

printf 'stage0: upstream %s at %s\n' "$upstream_real" "$head"
printf 'stage0: snapshot %s\n' "$snapshot"
git -C "$upstream_real" archive --format=tar "$pin" | tar -x -C "$snapshot"
[ ! -e "$snapshot/.git" ] || die "snapshot unexpectedly contains .git"

[ -f "$snapshot/Makefile" ] || die "snapshot is missing Makefile"
[ -f "$snapshot/src/main.c" ] || die "snapshot is missing src/main.c"
[ -f "$snapshot/include/global.h" ] || die "snapshot is missing include/global.h"

if [ -n "$rom" ]; then
    cp "$rom" "$snapshot/baserom_sa2.gba"
    printf 'stage0: ROM %s (SHA-1 %s) copied into the disposable snapshot\n' "$rom" "$rom_sha1"
else
    printf 'stage0: SA2_ROM not set; the portable decomp build does not need it\n'
fi

printf 'stage0: building upstream core with TAS_TESTING=%s\n' "$tas"
jobs=$(nproc 2>/dev/null || echo 2)
if ! "$MAKE" -C "$snapshot" -j"$jobs" sdl GAME_NAME=sa2 TAS_TESTING="$tas" >"$build_root/upstream-build.log" 2>&1; then
    tail -n 100 "$build_root/upstream-build.log" >&2
    die "upstream build failed; log: $build_root/upstream-build.log"
fi
[ -f "$snapshot/build/sdl/sa2/src/platform/pret_sdl/sdl2.o" ] || die "upstream SDL platform object is missing"
[ -f "$snapshot/libagbsyscall/build/sdl/libagbsyscall.a" ] || die "libagbsyscall is missing"

core_archive="$build_root/libsa2core.a"
rm -f "$core_archive"
mapfile -d '' core_objects < <(find "$snapshot/build/sdl/sa2" -name '*.o' ! -path '*/platform/pret_sdl/sdl2.o' -print0 | sort -z)
[ "${#core_objects[@]}" -gt 0 ] || die "no upstream core objects found"

printf 'stage0: archiving %d upstream objects (sdl2.o excluded)\n' "${#core_objects[@]}"
"$AR" rcs "$core_archive" "${core_objects[@]}"

if nm -g --defined-only "$core_archive" 2>/dev/null | grep -q ' T main$'; then
    die "the core archive defines main; the host owns it"
fi

sdl_cflags=($(sdl2-config --cflags))
sdl_libs=($(sdl2-config --libs))
common_flags=(
    -I"$snapshot/include"
    -I"$repo_root/remaster"
    -DGAME=GAME_SA2
    -DUSA
    -DPLATFORM_GBA=0
    -DPLATFORM_SDL=1
    -DPLATFORM_WIN32=0
    -DCPU_ARCH_X86=0
    -DCPU_ARCH_ARM=0
    -DPORTABLE=1
    -DDEBUG=0
    -DTAS_TESTING="$tas"
    -DENABLE_DECOMP_CREDITS=1
    -DRENDERER=RENDERER_SOFTWARE
)
host_flags=("${common_flags[@]}" -O2 -g -Wall -Wextra -Werror -std=gnu11 -I"$repo_root/remaster" "${sdl_cflags[@]}")
renderer_flags=("${common_flags[@]}" -O2 -g -Wall -Wextra -Werror -std=gnu11)

mkdir -p "$build_root/host"
printf 'stage0: compiling the host\n'
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/main.c" -o "$build_root/host/main.o"
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/present.c" -o "$build_root/host/present.o"
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/oam_bridge.c" -o "$build_root/host/oam_bridge.o"
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/script_input.c" -o "$build_root/host/script_input.o"
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/oam.c" -o "$build_root/host/oam.o"

printf 'stage0: compiling the BG-sprite composition module\n'
"$CC" "${renderer_flags[@]}" -c "$repo_root/remaster/renderer/bg_sprites.c" -o "$build_root/host/bg_sprites.o"
"$CC" "${renderer_flags[@]}" -c "$repo_root/remaster/renderer/bg_sprites_port.c" -o "$build_root/host/bg_sprites_port.o"

printf 'stage0: compiling the stage 1 layer path\n'
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/layer_path.c" -o "$build_root/host/layer_path.o"
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/snapshot.c" -o "$build_root/host/snapshot.o"
"$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/gl_api.c" -o "$build_root/host/gl_api.o"

printf 'stage0: linking the host against the core archive\n'
"$CC" "$build_root/host/main.o" "$build_root/host/present.o" \
    "$build_root/host/oam_bridge.o" "$build_root/host/script_input.o" \
    "$build_root/host/oam.o" \
    "$build_root/host/bg_sprites.o" "$build_root/host/bg_sprites_port.o" \
    "$build_root/host/layer_path.o" "$build_root/host/snapshot.o" "$build_root/host/gl_api.o" \
    "$core_archive" "$snapshot/libagbsyscall/build/sdl/libagbsyscall.a" \
    "${sdl_libs[@]}" -lm -Wl,--no-undefined -o "$build_root/sa2-stage0"

status_after=$(git -C "$upstream_real" status --porcelain --untracked-files=no)
[ "$status_before" = "$status_after" ] || die "the build changed tracked files in the upstream checkout"

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
    printf 'tas_testing=%s\n' "$tas"
    printf 'core_objects=%s\n' "${#core_objects[@]}"
    printf 'core_archive=%s\n' "$(hash_file "$core_archive")"
    printf 'main.c=%s\n' "$(hash_file "$repo_root/remaster/host/main.c")"
    printf 'present.c=%s\n' "$(hash_file "$repo_root/remaster/host/present.c")"
    printf 'present.h=%s\n' "$(hash_file "$repo_root/remaster/host/present.h")"
    printf 'oam_bridge.c=%s\n' "$(hash_file "$repo_root/remaster/host/oam_bridge.c")"
    printf 'oam_bridge.h=%s\n' "$(hash_file "$repo_root/remaster/host/oam_bridge.h")"
    printf 'script_input.c=%s\n' "$(hash_file "$repo_root/remaster/host/script_input.c")"
    printf 'script_input.h=%s\n' "$(hash_file "$repo_root/remaster/host/script_input.h")"
    printf 'oam.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/oam.c")"
    printf 'oam.h=%s\n' "$(hash_file "$repo_root/remaster/renderer/oam.h")"
    printf 'bg_sprites.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/bg_sprites.c")"
    printf 'bg_sprites.h=%s\n' "$(hash_file "$repo_root/remaster/renderer/bg_sprites.h")"
    printf 'bg_sprites_port.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/bg_sprites_port.c")"
    printf 'bg_sprites_port.h=%s\n' "$(hash_file "$repo_root/remaster/renderer/bg_sprites_port.h")"
    printf 'layer_path.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/layer_path.c")"
    printf 'snapshot.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/snapshot.c")"
    printf 'gl_api.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/gl_api.c")"
    printf 'stage1_layers.h=%s\n' "$(hash_file "$repo_root/remaster/shaders/stage1_layers.h")"
    if [ -n "$rom" ]; then
        printf 'rom_sha1=%s\n' "$rom_sha1"
    fi
} >"$build_root/provenance.txt"

printf 'stage0: ok; binary %s\n' "$build_root/sa2-stage0"
printf 'stage0: provenance %s\n' "$build_root/provenance.txt"
