# Frame capture

[Back to README](../README.md) · [Tracking](TRACKING.md) · [Contribution workflow](../CONTRIBUTING.md)

Every frame and clip under `docs/images/` was captured from a headless run of a
port built from [`SAT-R/sa2`](https://github.com/SAT-R/sa2) on 2026-10-19.
Nothing is redrawn, reconstructed, or taken from another build.

## What is needed

| Item | Value used here |
| :--- | :--- |
| Sonic Advance | Europe (En,Ja,Fr,De,Es), SHA-1 `eb00f101af23d728075ac2117e27ecd8a4b4c3e9` |
| Sonic Advance 2 | USA (En,Ja,Fr,De,Es,It), SHA-1 `7bcd6a07af7c894746fa28073fe0c0e34408022d` |
| Upstream checkout | `SAT-R/sa2` at `393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d` (2026-07-21), cloned outside this repository |
| Toolchain | GNU make, a C compiler, SDL2, Python 3 with Pillow |

The ROMs stay in the user's local folder (`.local/` is ignored) and are copied
into the upstream checkout as `baserom_sa1.gba` and `baserom_sa2.gba`. They are
never added to this repository, a log, or an image.

Upstream is unlicensed and its maintainers run a strict no-LLM contributions
policy. This repository does not vendor it, does not redistribute its code, and
does not propose changes back. The capture hook below is a local, uncommitted
change to that checkout.

## Build the ports

```sh
git clone https://github.com/SAT-R/sa2 /tmp/sa2
cp "/path/to/Sonic Advance (Europe) (En,Ja,Fr,De,Es).gba" /tmp/sa2/baserom_sa1.gba
cp "/path/to/Sonic Advance 2 (USA) (En,Ja,Fr,De,Es,It).gba" /tmp/sa2/baserom_sa2.gba
cd /tmp/sa2

make -j"$(nproc)" sdl GAME_NAME=sa1
make -j"$(nproc)" sdl GAME_NAME=sa2 TAS_TESTING=1
```

`TAS_TESTING=1` compiles the SA2 port with the recorded any% tape embedded in
`gInputPlaybackData` (`src/input_recorder.c`); the game plays itself. SA1 has no
such tape, so its run uses scripted input.

Both outputs are desktop SDL binaries at the port's widescreen size, 426x240.

## The local capture hook

The desktop SDL port draws with `gpsp_draw_frame(gameImage)` in its normal
loop, but its headless path returns before drawing. The hook adds two things to
`src/platform/pret_sdl/sdl2.c` (local only):

1. A frame writer that renders the 16-bit `gameImage` buffer and writes binary
   PPM:

```c
static int captureFrame = 0;

static void Capture_Frame(void)
{
    const char *dir = getenv("CAPTURE_DIR");
    int every, start;
    char path[1024];
    FILE *file;
    int i;

    if (dir == NULL) return;

    every = getenv("CAPTURE_EVERY") ? atoi(getenv("CAPTURE_EVERY")) : 6;
    if (every < 1) every = 1;
    start = getenv("CAPTURE_START") ? atoi(getenv("CAPTURE_START")) : 0;
    if (captureFrame < start) return;
    if (getenv("CAPTURE_MAX") && captureFrame >= atoi(getenv("CAPTURE_MAX"))) return;

    gpsp_draw_frame(gameImage);
    if (captureFrame % every != 0) return;

    snprintf(path, sizeof(path), "%s/frame_%06d.ppm", dir, captureFrame);
    file = fopen(path, "wb");
    if (file == NULL) return;

    fprintf(file, "P6\n%d %d\n255\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        u16 pixel = gameImage[i];
        fputc((pixel & 0x1F) << 3, file);
        fputc(((pixel >> 5) & 0x1F) << 3, file);
        fputc(((pixel >> 10) & 0x1F) << 3, file);
    }
    fclose(file);
}
```

2. A call in the headless branch of `VBlankIntrWait` that sets the key register
   from the script, handles the VBlank, dumps the frame, and advances the
   counter. The surrounding branch is upstream; these are the added lines:

```c
REG_KEYINPUT = KEYS_MASK ^ Capture_ScriptedKeys(captureFrame);
REG_VCOUNT = DISPLAY_HEIGHT + 1;
HANDLE_VBLANK_INTRS();
Capture_Frame();
captureFrame++;
return;
```

The scripted input for SA1:

```c
static u16 Capture_ScriptedKeys(int frame)
{
    const char *script = getenv("CAPTURE_SCRIPT");

    if (script == NULL) return 0;

    if (strcmp(script, "sa1") == 0) {
        u16 keys = 0;

        if (frame >= 600 && frame < 1100) {
            // Boot, title and menus: alternate A and START.
            if ((frame % 40) < 5) keys |= A_BUTTON;
            if ((frame % 40) >= 20 && (frame % 40) < 25) keys |= START_BUTTON;
        } else if (frame >= 1100) {
            // Run right and jump; no START, which pauses a running stage.
            keys |= DPAD_RIGHT;
            if ((frame % 150) < 6) keys |= A_BUTTON;
        }

        return keys;
    }

    return 0;
}
```

### Environment

| Variable | Meaning |
| :--- | :--- |
| `HEADLESS` | Must be the string `true`; any other value is ignored by the port. |
| `CAPTURE_DIR` | Directory for the PPM frames; it must already exist. |
| `CAPTURE_START` | First frame to dump (default `0`). |
| `CAPTURE_MAX` | Stop dumping at this frame (default: no limit). The game keeps running. |
| `CAPTURE_EVERY` | Dump every Nth frame (default `6`; `2` gives 30 fps at the game's 60 Hz). |
| `CAPTURE_SCRIPT` | `sa1` for the scripted run; unset for the SA2 tape. |

Frame numbers are the emulated VBlank count, not wall-clock time. A run keeps
executing after `CAPTURE_MAX`, so the commands below are wrapped in `timeout`.

## Runs

### Sonic Advance 2 (recorded tape)

```sh
HEADLESS=true CAPTURE_DIR=/tmp/cap_sa2 CAPTURE_EVERY=30 CAPTURE_MAX=7200 ./sa2.sdl
```

The tape plays from the title screen through Leaf Forest, Hot Crater and
beyond; the run ended by itself after the tape. The stills were selected from
this run.

For the clip, a second run dumped a window at 30 fps:

```sh
HEADLESS=true CAPTURE_DIR=/tmp/cap_sa2run CAPTURE_START=1000 CAPTURE_MAX=1240 CAPTURE_EVERY=2 ./sa2.sdl
```

The tape is deterministic: frame 1200 of either run is the same frame.

### Sonic Advance (scripted input)

```sh
HEADLESS=true CAPTURE_DIR=/tmp/cap_sa1 CAPTURE_SCRIPT=sa1 CAPTURE_EVERY=30 CAPTURE_MAX=7200 ./sa1.sdl
HEADLESS=true CAPTURE_DIR=/tmp/cap_sa1gif CAPTURE_SCRIPT=sa1 CAPTURE_EVERY=2 CAPTURE_START=1900 CAPTURE_MAX=2140 ./sa1.sdl
```

The script advances the boot, title and menus, then holds right and jumps. The
first file is written at frame 0, so the run also records the logos.

## Committed images

| File | Source | Scene |
| :--- | :--- | :--- |
| `sa2-leaf-forest.png` | SA2 tape, frame 1200 | Leaf Forest Act 1, Sonic running with rings |
| `sa2-leaf-forest.gif` | SA2 tape, frames 1000-1238, every 2 | Leaf Forest Act 1, running section |
| `sa2-leaf-forest-act-2.png` | SA2 tape, frame 3300 | Leaf Forest Act 2, Sonic and Tails in the air |
| `sa2-leaf-forest-boss.png` | SA2 tape, frame 5010 | Leaf Forest boss |
| `sa2-hot-crater.png` | SA2 tape, frame 6900 | Hot Crater Act 1 |
| `sa1-neo-green-hill.png` | SA1 script, frame 2100 | Neo Green Hill Zone, Sonic and Tails on the shore |
| `sa1-neo-green-hill.gif` | SA1 script, frames 1900-2138, every 2 | Neo Green Hill Zone, beach section |
| `sa1-neo-green-hill-jump.png` | SA1 script, frame 2610 | Neo Green Hill Zone, jump over water |

Stills were converted with:

```sh
python3 tools/ppm_to_png.py /tmp/cap_sa2/frame_001200.ppm docs/images/sa2-leaf-forest.png --scale 2
```

The conversion is a format change and a 2x nearest-neighbour upscale. Nothing
is filtered, sharpened, cropped, or recolored. Clips were assembled from
consecutive PPM frames with Pillow at 30 fps and a 96-colour palette, with no
other changes.

## Review the captures

```sh
python3 tools/make_contact_sheet.py /tmp/cap_sa2 /tmp/sheets-sa2
```

Both review tools require Pillow (`python3 -m pip install pillow`).

## Limitations

- The SA1 port is early: menus and some UI render corrupted in the upstream
  build ([upstream issue #274](https://github.com/SAT-R/sa2/issues/274)), and
  the HUD is not visible in these captures. Only clean scenes were kept.
- The SA1 run depends on the scripted schedule; a change in the port's boot
  timing can shift the frame numbers.
- The SA2 tape recorded a full any% run; its zone order and frame numbers are
  fixed by the tape.
- The captures are 426x240, the port's widescreen output, not pixel-perfect
  GBA frames.
