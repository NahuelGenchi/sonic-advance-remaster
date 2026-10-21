#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds and runs the stage 0 controller mapping tests.
#
# The test is self-contained: it compiles the SDL-free mapping module and its
# test program, and it needs no upstream checkout, no ROM and no display.
#
# Usage:
#   tools/pad_test.sh
#
# Environment:
#   SAR_TEST_BUILD_ROOT  Build directory. Default: ${TMPDIR:-/tmp}/sar-pad-test.
#   CC                   C compiler override.

set -euo pipefail

die() {
    printf 'pad_test: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/.." && pwd -P)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || die "tool not found: $CC"

build_root=${SAR_TEST_BUILD_ROOT:-"${TMPDIR:-/tmp}/sar-pad-test"}
mkdir -p "$build_root"

printf 'pad_test: compiling the controller mapping module\n'
"$CC" -std=gnu11 -O2 -g -Wall -Wextra -Werror -I"$repo_root/remaster/host" \
    -c "$repo_root/remaster/host/pad_input.c" -o "$build_root/pad_input.o"
"$CC" -std=gnu11 -O2 -g -Wall -Wextra -Werror -I"$repo_root/remaster/host" \
    -c "$repo_root/remaster/host/pad_input_test.c" -o "$build_root/pad_input_test.o"

printf 'pad_test: linking\n'
"$CC" "$build_root/pad_input.o" "$build_root/pad_input_test.o" -o "$build_root/pad_test"

printf 'pad_test: running\n'
"$build_root/pad_test"

printf 'pad_test: ok\n'
