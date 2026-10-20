#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds and runs the canonical OAM tests.
#
# The test is self-contained: it compiles the renderer-owned OAM module and
# its test program, and it needs no upstream checkout, no ROM and no display.
#
# Usage:
#   tools/oam_test.sh
#
# Environment:
#   SAR_TEST_BUILD_ROOT  Build directory. Default: ${TMPDIR:-/tmp}/sar-oam-test.
#   CC                   C compiler override.

set -euo pipefail

die() {
    printf 'oam_test: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/.." && pwd -P)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || die "tool not found: $CC"

build_root=${SAR_TEST_BUILD_ROOT:-"${TMPDIR:-/tmp}/sar-oam-test"}
mkdir -p "$build_root"

printf 'oam_test: compiling the canonical OAM module\n'
"$CC" -std=gnu11 -O2 -g -Wall -Wextra -Werror -I"$repo_root/remaster/renderer" \
    -c "$repo_root/remaster/renderer/oam.c" -o "$build_root/oam.o"
"$CC" -std=gnu11 -O2 -g -Wall -Wextra -Werror -I"$repo_root/remaster/renderer" \
    -c "$repo_root/remaster/renderer/oam_test.c" -o "$build_root/oam_test.o"

printf 'oam_test: linking\n'
"$CC" "$build_root/oam.o" "$build_root/oam_test.o" -o "$build_root/oam_test"

printf 'oam_test: running\n'
"$build_root/oam_test"
