#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds and runs the UI field tests.
#
# The test is self-contained: it compiles the renderer-owned UI field module,
# the canonical OAM module it reads and the test program, and it needs no
# upstream checkout, no ROM and no display.
#
# Usage:
#   tools/ui_viewport_test.sh
#
# Environment:
#   SAR_TEST_BUILD_ROOT  Build directory. Default: ${TMPDIR:-/tmp}/sar-ui-viewport-test.
#   CC                   C compiler override.

set -euo pipefail

die() {
    printf 'ui_viewport_test: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/.." && pwd -P)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || die "tool not found: $CC"

build_root=${SAR_TEST_BUILD_ROOT:-"${TMPDIR:-/tmp}/sar-ui-viewport-test"}
mkdir -p "$build_root"

common_flags=(-std=gnu11 -O2 -g -Wall -Wextra -Werror -I"$repo_root/remaster/renderer")

printf 'ui_viewport_test: compiling the UI field module\n'
"$CC" "${common_flags[@]}" -c "$repo_root/remaster/renderer/ui_viewport.c" -o "$build_root/ui_viewport.o"
"$CC" "${common_flags[@]}" -c "$repo_root/remaster/renderer/oam.c" -o "$build_root/oam.o"
"$CC" "${common_flags[@]}" -c "$repo_root/remaster/renderer/tests/ui_viewport_test.c" -o "$build_root/ui_viewport_test.o"

printf 'ui_viewport_test: linking\n'
"$CC" "$build_root/ui_viewport.o" "$build_root/oam.o" "$build_root/ui_viewport_test.o" \
    -o "$build_root/ui_viewport_test"

printf 'ui_viewport_test: running\n'
"$build_root/ui_viewport_test"
