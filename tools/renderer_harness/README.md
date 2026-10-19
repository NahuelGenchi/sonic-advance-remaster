# Renderer harness

This is the smallest executable that proves the M2 integration boundary, not a
renderer. It compiles two upstream translation units from an external
[`SAT-R/sa2`](https://github.com/SAT-R/sa2) checkout, archives them, and links a
host owned by this repository against the archive. The host calls the upstream
software PPU through its public C symbol and checks the result.

What it establishes:

- Upstream can be consumed from outside this repository without copying or
  patching it.
- The upstream headers and the project's compile definitions are sufficient to
  build the software PPU.
- A host executable from this repository can provide `main`, the interrupt table
  and the DMA boundary, and link the upstream core as an archive.
- The harness reads the upstream tree and leaves tracked files unchanged.

What it does not establish, and does not claim:

- A full game build, a ROM run, a frame capture, or any remaster renderer.
- The full platform replacement (input, audio, save files, window and GL
  context). Those are M2 implementation work.
- Performance, visual parity, or a working 2.5D lighting path.

## Prerequisites

A checkout of `SAT-R/sa2` at the commit in `tools/upstream.pin`, outside this
repository. No ROM is needed.

```sh
git clone https://github.com/SAT-R/sa2 /tmp/sa2
git -C /tmp/sa2 checkout 393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d
```

A C compiler, a C++ compiler and `ar` are required. The script uses `cc`, `c++`
and `ar` unless `CC`, `CXX` and `AR` say otherwise.

## Run

From the repository root:

```sh
SA2_UPSTREAM=/tmp/sa2 tools/renderer_harness/build.sh
```

Expected output:

```text
renderer-harness: upstream /tmp/sa2 at 393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d
renderer-harness: compiling upstream common.c
renderer-harness: compiling upstream gpsp_renderer.cc
renderer-harness: compiling host probe.c
renderer-harness: archiving the upstream core
renderer-harness: linking the host against the archive
renderer-harness: running the host
harness: upstream software PPU 426x240
harness: upstream interrupt dispatch ok
harness: forced-blank checksum 5873dfc0
harness: first=ffff last=ffff
renderer-harness: ok; artifacts in .../build/renderer_harness
```

The `checksum` is over a forced-blank 426x240 framebuffer. It is a deterministic
fingerprint of the pinned PPU path, not a game frame.

## Fail-closed checks

The script stops if:

- `SA2_UPSTREAM` is unset, missing, or resolves inside this repository.
- The checkout is not a git work tree.
- Tracked files in the checkout are modified.
- `HEAD` is not the commit in `tools/upstream.pin`.
- The upstream PPU or headers are missing.
- The harness output is not the expected pinned output.
- The build changes tracked files in the upstream checkout.

`SAR_ALLOW_DIRTY_UPSTREAM=1` and `SAR_ALLOW_OTHER_REV=1` exist only for deliberate
local experiments; they are not a supported build mode. `SAR_BUILD_ROOT` moves
the ignored build output, but it may not point inside the upstream checkout.

## Artifacts

Everything is written to `build/renderer_harness/` (git-ignored) unless
`SAR_BUILD_ROOT` is set:

- `common.o`, `gpsp_renderer.o`, `probe.o`: object files.
- `libsa2core_min.a`: the two upstream objects, archived.
- `probe`: the linked host.
- `harness.log`: the output above.
- `provenance.txt`: upstream path and commit, plus SHA-256 hashes of the two
  upstream files and the probe.

The archive, objects and provenance stay local. Upstream source, diffs and
binaries are never committed.
