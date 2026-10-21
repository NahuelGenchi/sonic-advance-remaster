#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# M2 stage 0 build driver.
#
# Validates a pinned, clean, external SAT-R/sa2 checkout, snapshots it outside
# this repository, builds the selected upstream core objects, archives
# everything except the upstream SDL platform object, then compiles and links
# the project-owned host from remaster/host/.
#
# Usage:
#   SA2_UPSTREAM=/path/to/sa2 tools/stage0/build.sh
#   SAR_GAME=sa1 SA2_UPSTREAM=/path/to/sa2 tools/stage0/build.sh
#   SAR_GAME=both SA2_UPSTREAM=/path/to/sa2 tools/stage0/build.sh
#
# Environment:
#   SA2_UPSTREAM    Required. External SAT-R/sa2 checkout at tools/upstream.pin.
#   SAR_GAME        sa2 (default), sa1, or both.
#   SA1_ROM         Optional. Sonic Advance 1 (Europe) ROM; validated by SHA-1
#                   and copied into the disposable snapshot as baserom_sa1.gba.
#   SA2_ROM         Optional. Sonic Advance 2 (USA) ROM; validated by SHA-1 and
#                   copied into the disposable snapshot as baserom_sa2.gba.
#   SAR_BUILD_ROOT  Build directory. Default: ${TMPDIR:-/tmp}/sar-stage0.
#   SAR_TAS_TESTING 1 (default) builds the recorded SA2 tape; 0 is interactive.
#                   The Sonic Advance 1 target has no tape and is always built
#                   interactive.
#   CC, AR, MAKE    Tool overrides.

set -euo pipefail

die() {
    printf 'stage0: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/../.." && pwd -P)
pin_file="$repo_root/tools/upstream.pin"
rom_sa1_sha1_expected="eb00f101af23d728075ac2117e27ecd8a4b4c3e9"
rom_sa2_sha1_expected="7bcd6a07af7c894746fa28073fe0c0e34408022d"

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

game_list=${SAR_GAME:-sa2}
case "$game_list" in
    sa1) games=(sa1) ;;
    sa2) games=(sa2) ;;
    both) games=(sa1 sa2) ;;
    *) die "SAR_GAME must be sa1, sa2 or both" ;;
esac

tas=${SAR_TAS_TESTING:-1}
case "$tas" in
    0 | 1) ;;
    *) die "SAR_TAS_TESTING must be 0 or 1" ;;
esac

if [ "$tas" = 1 ] && [ "$game_list" = sa1 ]; then
    printf 'stage0: the Sonic Advance 1 target has no recorded tape; building it interactive\n'
fi

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

rom_sa1=${SA1_ROM:-}
rom_sa1_sha1=
if [ -n "$rom_sa1" ]; then
    [ -f "$rom_sa1" ] || die "SA1_ROM is not a file: $rom_sa1"
    rom_sa1_sha1=$(sha1sum "$rom_sa1" | awk '{ print $1 }')
    [ "$rom_sa1_sha1" = "$rom_sa1_sha1_expected" ] \
        || die "SA1_ROM has SHA-1 $rom_sa1_sha1, expected $rom_sa1_sha1_expected"
fi

rom_sa2=${SA2_ROM:-}
rom_sa2_sha1=
if [ -n "$rom_sa2" ]; then
    [ -f "$rom_sa2" ] || die "SA2_ROM is not a file: $rom_sa2"
    rom_sa2_sha1=$(sha1sum "$rom_sa2" | awk '{ print $1 }')
    [ "$rom_sa2_sha1" = "$rom_sa2_sha1_expected" ] \
        || die "SA2_ROM has SHA-1 $rom_sa2_sha1, expected $rom_sa2_sha1_expected"
fi

snapshot="$build_root/source"
mkdir -p "$build_root"
rm -rf "$snapshot"
mkdir -p "$snapshot"

printf 'stage0: upstream %s at %s\n' "$upstream_real" "$head"
printf 'stage0: snapshot %s\n' "$snapshot"
printf 'stage0: game target %s\n' "$game_list"
git -C "$upstream_real" archive --format=tar "$pin" | tar -x -C "$snapshot"
[ ! -e "$snapshot/.git" ] || die "snapshot unexpectedly contains .git"

[ -f "$snapshot/Makefile" ] || die "snapshot is missing Makefile"
[ -f "$snapshot/src/main.c" ] || die "snapshot is missing src/main.c"
[ -f "$snapshot/include/global.h" ] || die "snapshot is missing include/global.h"

if [ "$game_list" != sa2 ]; then
    if [ -n "$rom_sa1" ]; then
        cp "$rom_sa1" "$snapshot/baserom_sa1.gba"
        printf 'stage0: SA1 ROM %s (SHA-1 %s) copied into the disposable snapshot\n' "$rom_sa1" "$rom_sa1_sha1"
    else
        printf 'stage0: SA1_ROM not set; the portable decomp build does not need it\n'
    fi
fi

if [ "$game_list" != sa1 ]; then
    if [ -n "$rom_sa2" ]; then
        cp "$rom_sa2" "$snapshot/baserom_sa2.gba"
        printf 'stage0: SA2 ROM %s (SHA-1 %s) copied into the disposable snapshot\n' "$rom_sa2" "$rom_sa2_sha1"
    else
        printf 'stage0: SA2_ROM not set; the portable decomp build does not need it\n'
    fi
fi

jobs=$(nproc 2>/dev/null || echo 2)
sdl_cflags=($(sdl2-config --cflags))
sdl_libs=($(sdl2-config --libs))

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{ print $1 }'
    else
        shasum -a 256 "$1" | awk '{ print $1 }'
    fi
}

for game in "${games[@]}"; do
    case "$game" in
        sa1)
            game_define=GAME_SA1
            game_region=EUROPE
            game_credits=0
            game_tas=0
            game_rom_sha1=$rom_sa1_sha1
            ;;
        sa2)
            game_define=GAME_SA2
            game_region=USA
            game_credits=1
            game_tas=$tas
            game_rom_sha1=$rom_sa2_sha1
            ;;
    esac

    build_log="$build_root/upstream-build-$game.log"
    printf 'stage0: building %s upstream core with TAS_TESTING=%s\n' "$game" "$game_tas"
    if ! "$MAKE" -C "$snapshot" -j"$jobs" sdl GAME_NAME="$game" \
        TAS_TESTING="$game_tas" ENABLE_DECOMP_CREDITS="$game_credits" >"$build_log" 2>&1; then
        tail -n 100 "$build_log" >&2
        die "upstream build failed; log: $build_log"
    fi

    sdl_object="$snapshot/build/sdl/$game/src/platform/pret_sdl/sdl2.o"
    [ -f "$sdl_object" ] || die "upstream SDL platform object is missing: $sdl_object"
    [ -f "$snapshot/libagbsyscall/build/sdl/libagbsyscall.a" ] || die "libagbsyscall is missing"

    core_archive="$build_root/lib${game}core.a"
    rm -f "$core_archive"
    mapfile -d '' core_objects < <(find "$snapshot/build/sdl/$game" -name '*.o' ! -path '*/platform/pret_sdl/sdl2.o' -print0 | sort -z)
    [ "${#core_objects[@]}" -gt 0 ] || die "no upstream core objects found for $game"

    printf 'stage0: archiving %d %s upstream objects (sdl2.o excluded)\n' "${#core_objects[@]}" "$game"
    "$AR" rcs "$core_archive" "${core_objects[@]}"

    if nm -g --defined-only "$core_archive" 2>/dev/null | grep -q ' T main$'; then
        die "the $game core archive defines main; the host owns it"
    fi

    common_flags=(
        -I"$snapshot/include"
        -I"$repo_root/remaster"
        -D"GAME=$game_define"
        -D"$game_region"
        -DPLATFORM_GBA=0
        -DPLATFORM_SDL=1
        -DPLATFORM_WIN32=0
        -DCPU_ARCH_X86=0
        -DCPU_ARCH_ARM=0
        -DPORTABLE=1
        -DDEBUG=0
        -DTAS_TESTING="$game_tas"
        -DENABLE_DECOMP_CREDITS="$game_credits"
        -DRENDERER=RENDERER_SOFTWARE
    )
    host_flags=("${common_flags[@]}" -O2 -g -Wall -Wextra -Werror -std=gnu11 -I"$repo_root/remaster" "${sdl_cflags[@]}")
    renderer_flags=("${common_flags[@]}" -O2 -g -Wall -Wextra -Werror -std=gnu11)

    host_dir="$build_root/host/$game"
    mkdir -p "$host_dir"
    printf 'stage0: compiling the %s host\n' "$game"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/main.c" -o "$host_dir/main.o"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/present.c" -o "$host_dir/present.o"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/oam_bridge.c" -o "$host_dir/oam_bridge.o"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/host/script_input.c" -o "$host_dir/script_input.o"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/oam.c" -o "$host_dir/oam.o"

    printf 'stage0: compiling the %s BG-sprite composition module\n' "$game"
    "$CC" "${renderer_flags[@]}" -c "$repo_root/remaster/renderer/bg_sprites.c" -o "$host_dir/bg_sprites.o"
    "$CC" "${renderer_flags[@]}" -c "$repo_root/remaster/renderer/bg_sprites_port.c" -o "$host_dir/bg_sprites_port.o"

    printf 'stage0: compiling the %s stage 1 layer path\n' "$game"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/layer_path.c" -o "$host_dir/layer_path.o"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/snapshot.c" -o "$host_dir/snapshot.o"
    "$CC" "${host_flags[@]}" -c "$repo_root/remaster/renderer/gl_api.c" -o "$host_dir/gl_api.o"

    printf 'stage0: compiling the %s UI viewport module\n' "$game"
    "$CC" "${renderer_flags[@]}" -c "$repo_root/remaster/renderer/ui_viewport.c" -o "$host_dir/ui_viewport.o"
    "$CC" "${renderer_flags[@]}" -c "$repo_root/remaster/renderer/ui_viewport_port.c" -o "$host_dir/ui_viewport_port.o"

    printf 'stage0: linking the %s host against the core archive\n' "$game"
    "$CC" "$host_dir/main.o" "$host_dir/present.o" \
        "$host_dir/oam_bridge.o" "$host_dir/script_input.o" \
        "$host_dir/oam.o" \
        "$host_dir/bg_sprites.o" "$host_dir/bg_sprites_port.o" \
        "$host_dir/layer_path.o" "$host_dir/snapshot.o" "$host_dir/gl_api.o" \
        "$host_dir/ui_viewport.o" "$host_dir/ui_viewport_port.o" \
        "$core_archive" "$snapshot/libagbsyscall/build/sdl/libagbsyscall.a" \
        "${sdl_libs[@]}" -lm -Wl,--no-undefined -o "$build_root/$game-stage0"

    {
        printf 'game=%s\n' "$game"
        printf 'upstream=%s\n' "$upstream_real"
        printf 'commit=%s\n' "$head"
        printf 'pin=%s\n' "$pin"
        printf 'tas_testing=%s\n' "$game_tas"
        printf 'enable_decomp_credits=%s\n' "$game_credits"
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
        printf 'ui_viewport.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/ui_viewport.c")"
        printf 'ui_viewport.h=%s\n' "$(hash_file "$repo_root/remaster/renderer/ui_viewport.h")"
        printf 'ui_viewport_port.c=%s\n' "$(hash_file "$repo_root/remaster/renderer/ui_viewport_port.c")"
        printf 'ui_viewport_port.h=%s\n' "$(hash_file "$repo_root/remaster/renderer/ui_viewport_port.h")"
        if [ -n "$game_rom_sha1" ]; then
            printf 'rom_sha1=%s\n' "$game_rom_sha1"
        fi
    } >"$build_root/provenance-$game.txt"

    if [ "${#games[@]}" -eq 1 ]; then
        cp "$build_log" "$build_root/upstream-build.log"
        cp "$build_root/provenance-$game.txt" "$build_root/provenance.txt"
    fi
done

status_after=$(git -C "$upstream_real" status --porcelain --untracked-files=no)
[ "$status_before" = "$status_after" ] || die "the build changed tracked files in the upstream checkout"

for game in "${games[@]}"; do
    printf 'stage0: ok; %s binary %s\n' "$game" "$build_root/$game-stage0"
    printf 'stage0: %s provenance %s\n' "$game" "$build_root/provenance-$game.txt"
done
