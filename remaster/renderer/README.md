# M2 renderer

[Back to README](../README.md) · [Renderer architecture](../docs/RENDERER.md) ·
[BG-sprite composition](../docs/BG-SPRITES.md) · [Stage 0 host](../tools/stage0/README.md)

This directory holds the project's own renderer code. It never contains
upstream source: the upstream game core is linked from an external checkout
(see `tools/stage0/README.md`).

## `bg_sprites`

`bg_sprites.c` and `bg_sprites.h` compose the game's "BG sprites" (full
background pictures that are displayed through a text background map) into
the background tilemaps, and apply the text-BG map fills the game requests.
`bg_sprites_port.c` is the adapter that reads the upstream globals; it is
the only file here that includes upstream headers.

The design, the VBlank order, the map geometry rules and the recorded
verification are in [`docs/BG-SPRITES.md`](../docs/BG-SPRITES.md).

## Tests

The composition module has no upstream dependency, so it is tested on its
own:

```sh
cc -std=c11 -Wall -Wextra -Werror -I remaster/renderer \
   remaster/renderer/bg_sprites.c remaster/renderer/tests/bg_sprites_test.c \
   -o /tmp/bg_sprites_test && /tmp/bg_sprites_test
```

```text
bg_sprites_test: all checks passed
```

The tests cover the map geometry (the four text map sizes, the four affine
map sizes, the map base and the row stride), the map fills, the composition
of several sprites on layers with different map sizes, the X and Y flips,
the palette bits, a sprite that moves, and the release of the entries a
layer's new owner has taken over.
