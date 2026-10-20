/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * M2 stage 0 host. This file owns the process entry point and the platform
 * seam that the upstream SDL platform used to own: SDL video/input/audio, the
 * save file, VBlank dispatch, and the software-PPU presentation path.
 *
 * The game logic, software PPU and shared platform helpers come from an
 * upstream core archive built from an external SAT-R/sa2 checkout. No upstream
 * source is compiled into this file and no ROM is loaded here.
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "core.h"
#include "gba/io_reg.h"
#include "gba/syscall.h"
#include "lib/agb_flash/flash_internal.h"
#include "platform/shared/audio/cgb_audio.h"
#include "platform/shared/dma.h"
#include "platform/shared/video/gpsp_renderer.h"
#include "oam_bridge.h"
#include "present.h"
#include "renderer/bg_sprites_port.h"
#include "renderer/layer_path.h"
#include "renderer/snapshot.h"
#include "renderer/ui_viewport.h"
#include "renderer/ui_viewport_port.h"
#include "script_input.h"

extern void AgbMain(void);

ALIGNED(256) uint16_t gameImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];

static SDL_Window *window;
static SDL_GLContext gl_context;
static SDL_AudioDeviceID audio_device;
static FILE *save_file;
static FILE *key_log;
static u16 keys;
static u16 last_logged_keys;
static int running = 1;
static int shutting_down;
static int audio_ready;
static unsigned long long frame_counter;
static unsigned long long capture_frames[32];
static int capture_frame_count;
static unsigned long long exit_after_frame;
static const char *capture_dir;
static int renderer_gl;
static SarSnapshot snapshot;
static Uint64 run_start;
static Uint64 tick_frequency;
static Uint64 next_tick;

static void SaveOpen(void);
static void SaveStore(void);
static void SaveClose(void);
static void Shutdown(int code);

void DoSoftReset(void) {}

static unsigned long long ReadFrameEnv(const char *name, int *present)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long long result;

    *present = 0;
    if (value == NULL || *value == '\0') {
        return 0;
    }

    result = strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        fprintf(stderr, "stage0: %s is not a frame number: %s\n", name, value);
        Shutdown(2);
    }

    *present = 1;
    return result;
}

// Parses a comma separated list of VBlank numbers. The stage 0 host
// accepted a single number here; the list keeps the same meaning for one
// entry and lets a recorded run capture several reference frames.
static void ReadCaptureFrames(const char *value)
{
    const char *cursor = value;

    while (*cursor != '\0' && capture_frame_count < (int)(sizeof(capture_frames) / sizeof(capture_frames[0]))) {
        char *end = NULL;
        unsigned long long frame;

        while (*cursor == ',' || *cursor == ' ') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        frame = strtoull(cursor, &end, 10);
        if (end == cursor || ((*end != '\0') && (*end != ','))) {
            fprintf(stderr, "stage0: SAR_CAPTURE_FRAME is not a frame number: %s\n", cursor);
            Shutdown(2);
        }

        capture_frames[capture_frame_count++] = frame;
        cursor = end;
    }

    if (capture_frame_count == 0) {
        fprintf(stderr, "stage0: SAR_CAPTURE_FRAME needs at least one frame number\n");
        Shutdown(2);
    }
}

static int IsCaptureFrame(unsigned long long frame)
{
    int i;

    for (i = 0; i < capture_frame_count; i++) {
        if (capture_frames[i] == frame) {
            return 1;
        }
    }

    return 0;
}

static void SaveOpen(void)
{
    const char *path = getenv("SAR_SAVE_PATH");
    long file_size;
    size_t bytes_to_read;
    size_t bytes_read;
    size_t i;

    if (path == NULL || *path == '\0') {
        path = "sa2.sav";
    }

    save_file = fopen(path, "r+b");
    if (save_file == NULL) {
        save_file = fopen(path, "w+b");
    }

    if (save_file == NULL) {
        fprintf(stderr, "stage0: cannot open save file %s; starting with a blank flash image\n", path);
        memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
        return;
    }

    fseek(save_file, 0, SEEK_END);
    file_size = ftell(save_file);
    fseek(save_file, 0, SEEK_SET);

    bytes_to_read = sizeof(FLASH_BASE);
    if (file_size >= 0 && (unsigned long)file_size < bytes_to_read) {
        bytes_to_read = (size_t)file_size;
    }

    bytes_read = fread(FLASH_BASE, 1, bytes_to_read, save_file);
    for (i = bytes_read; i < sizeof(FLASH_BASE); i++) {
        FLASH_BASE[i] = 0xFF;
    }
}

static void SaveStore(void)
{
    if (save_file == NULL) {
        return;
    }

    fseek(save_file, 0, SEEK_SET);
    fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), save_file);
    fflush(save_file);
}

static void SaveClose(void)
{
    if (save_file != NULL) {
        fclose(save_file);
        save_file = NULL;
    }
}

static void AudioInit(void)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    // The mixer needs a sample rate even when no SDL audio device opens.
    cgb_audio_init(48000);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "stage0: SDL audio unavailable: %s\n", SDL_GetError());
        return;
    }

    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 800;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0) {
        fprintf(stderr, "stage0: could not open audio: %s\n", SDL_GetError());
        return;
    }

    SDL_PauseAudioDevice(audio_device, 0);
    audio_ready = 1;
    printf("stage0: audio %d Hz, %d channels, %s\n", have.freq, have.channels,
           have.format == AUDIO_S16SYS ? "s16" : "other format");
}

static void SetKey(u16 bit, int down)
{
    if (down) {
        keys |= bit;
    } else {
        keys &= (u16)~bit;
    }
}

static void ProcessEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (event.key.repeat) {
                    break;
                }
                switch (event.key.keysym.sym) {
                    case SDLK_c:
                        SetKey(A_BUTTON, 1);
                        break;
                    case SDLK_x:
                        SetKey(B_BUTTON, 1);
                        break;
                    case SDLK_RETURN:
                        SetKey(START_BUTTON, 1);
                        break;
                    case SDLK_BACKSLASH:
                        SetKey(SELECT_BUTTON, 1);
                        break;
                    case SDLK_s:
                        SetKey(L_BUTTON, 1);
                        break;
                    case SDLK_d:
                        SetKey(R_BUTTON, 1);
                        break;
                    case SDLK_UP:
                        SetKey(DPAD_UP, 1);
                        break;
                    case SDLK_DOWN:
                        SetKey(DPAD_DOWN, 1);
                        break;
                    case SDLK_LEFT:
                        SetKey(DPAD_LEFT, 1);
                        break;
                    case SDLK_RIGHT:
                        SetKey(DPAD_RIGHT, 1);
                        break;
                    case SDLK_ESCAPE:
                        running = 0;
                        break;
                }
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                    case SDLK_c:
                        SetKey(A_BUTTON, 0);
                        break;
                    case SDLK_x:
                        SetKey(B_BUTTON, 0);
                        break;
                    case SDLK_RETURN:
                        SetKey(START_BUTTON, 0);
                        break;
                    case SDLK_BACKSLASH:
                        SetKey(SELECT_BUTTON, 0);
                        break;
                    case SDLK_s:
                        SetKey(L_BUTTON, 0);
                        break;
                    case SDLK_d:
                        SetKey(R_BUTTON, 0);
                        break;
                    case SDLK_UP:
                        SetKey(DPAD_UP, 0);
                        break;
                    case SDLK_DOWN:
                        SetKey(DPAD_DOWN, 0);
                        break;
                    case SDLK_LEFT:
                        SetKey(DPAD_LEFT, 0);
                        break;
                    case SDLK_RIGHT:
                        SetKey(DPAD_RIGHT, 0);
                        break;
                }
                break;
        }
    }
}

static void HandleVBlankInterrupts(void)
{
    REG_DISPSTAT |= INTR_FLAG_VBLANK;
    RunDMAs(DMA_VBLANK);
    if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR) {
        gIntrTable[INTR_INDEX_VBLANK]();
    }
    REG_DISPSTAT &= (u16)~INTR_FLAG_VBLANK;
}

static int WriteGameImagePpm(const char *path)
{
    FILE *file;
    int i;

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "stage0: cannot open %s for writing\n", path);
        return 0;
    }

    fprintf(file, "P6\n%d %d\n255\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        uint16_t pixel = gameImage[i];

        fputc((pixel & 0x1F) << 3, file);
        fputc(((pixel >> 5) & 0x1F) << 3, file);
        fputc(((pixel >> 10) & 0x1F) << 3, file);
    }

    fclose(file);
    return 1;
}

static void CaptureFrame(void)
{
    char game_path[1024];
    char gl_path[1024];

    if (capture_dir == NULL || !IsCaptureFrame(frame_counter)) {
        return;
    }

    if (snprintf(game_path, sizeof(game_path), "%s/frame_%06llu.ppm", capture_dir, frame_counter) >= (int)sizeof(game_path)
        || snprintf(gl_path, sizeof(gl_path), "%s/frame_%06llu.gl.ppm", capture_dir, frame_counter) >= (int)sizeof(gl_path)) {
        fprintf(stderr, "stage0: capture path is too long\n");
        Shutdown(1);
    }

    // The GPU path does not run the software PPU, so it has no software frame
    // to write; the readback of the presented frame is the capture.
    if (!renderer_gl) {
        if (!WriteGameImagePpm(game_path)) {
            Shutdown(1);
        }
        if (!SarPresentCaptureGlPpm(gl_path)) {
            Shutdown(1);
        }
        printf("stage0: captured frame %llu (%s, %s)\n", frame_counter, game_path, gl_path);
    } else {
        if (!SarPresentCaptureGlPpm(gl_path)) {
            Shutdown(1);
        }
        printf("stage1: captured frame %llu (%s)\n", frame_counter, gl_path);
    }
}

static void Pace(void)
{
    Uint64 now;
    Uint64 interval;
    Uint64 remaining;
    Uint64 milliseconds;

    if (tick_frequency == 0) {
        return;
    }

    interval = tick_frequency / 60u;
    now = SDL_GetPerformanceCounter();

    if (next_tick == 0 || now > next_tick + interval) {
        next_tick = now;
    }

    if (next_tick > now) {
        remaining = next_tick - now;
        milliseconds = remaining * 1000u / tick_frequency;
        if (milliseconds > 1) {
            SDL_Delay((Uint32)(milliseconds - 1));
        }
        while (SDL_GetPerformanceCounter() < next_tick) {
            // Spin the last fraction of a millisecond.
        }
    }

    next_tick += interval;
}

static void Shutdown(int code)
{
    if (shutting_down) {
        exit(code);
    }
    shutting_down = 1;

    SaveStore();
    SaveClose();

    if (key_log != NULL) {
        fclose(key_log);
        key_log = NULL;
    }

    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    if (run_start != 0 && tick_frequency != 0) {
        Uint64 elapsed = SDL_GetPerformanceCounter() - run_start;

        printf("stage1: %llu frames in %.3f s (%.3f Hz)\n", frame_counter, (double)elapsed / (double)tick_frequency,
               (double)frame_counter * (double)tick_frequency / (double)elapsed);
    }

    SarLayerPathShutdown();
    SarPresentShutdown();

    if (gl_context != NULL) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }

    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    SDL_Quit();
    exit(code);
}

void Platform_StoreSaveFile(void) { SaveStore(); }

void Platform_QueueAudio(const s16 *data, u32 numBytes)
{
    if (!audio_ready || audio_device == 0) {
        return;
    }

    // Keep the queue near one tick of audio; a stalled or fast display must not
    // turn into unbounded latency.
    if (SDL_GetQueuedAudioSize(audio_device) > numBytes * 10u) {
        SDL_ClearQueuedAudio(audio_device);
    }

    SDL_QueueAudio(audio_device, data, numBytes);
}

u16 Platform_GetKeyInput(void)
{
    if (SarScriptEnabled()) {
        // A recorded run replays the documented capture script instead of
        // the keyboard.
        return (u16)SarScriptedKeys(frame_counter);
    }

    if (key_log != NULL && keys != last_logged_keys) {
        fprintf(key_log, "frame=%llu keys=%04x\n", frame_counter, keys);
        fflush(key_log);
        last_logged_keys = keys;
    }

    return keys;
}

void VBlankIntrWait(void)
{
    ProcessEvents();

    if (!running) {
        Shutdown(0);
    }

    REG_KEYINPUT = KEYS_MASK ^ Platform_GetKeyInput();
    REG_VCOUNT = DISPLAY_HEIGHT + 1;

    // The game registered its BG sprites during the tick that just ended.
    // Snapshot them before the VBlank handlers run: upstream clears that
    // state while it copies the queued character data into VRAM.
    SarBgSpritesBeginVBlank();
    HandleVBlankInterrupts();

    // The engine writes two OAM record layouts in the same build. The
    // bridge converts the GBA-native ones into the extended layout the
    // software PPU reads, and keeps the canonical table for the renderer.
    SarOamBridgeCapture();

    // Compose the BG-sprite layers after the VBlank handlers (so the map is
    // written after the character data it references) and before the software
    // PPU draws the frame.
    SarBgSpritesCompose();

    // The scene is the coordinate space of the frame. The engine's stage
    // task is the engine's own statement of whether the 426x240 gameplay
    // world is on screen, so it is what the viewport reads; the viewport
    // never guesses the scene from the frame's contents.
    sar_ui_viewport_set_scene(sar_ui_viewport_port_scene());

    if (sar_ui_viewport_get_scene() == SAR_UI_SCENE_FIELD) {
        // A UI field is composed at 240x160 from the engine state and then
        // presented with one uniform scale and letterboxing. The gameplay
        // world is not rendered for this frame, so no cropped gameplay frame
        // is left behind when the scene changes.
        sar_ui_viewport_port_render_field();
        sar_ui_viewport_present(sar_ui_viewport_port_field(), gameImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        SarPresentFrame(gameImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else if (renderer_gl) {
        // The GPU layer path replaces the software PPU for this frame. It
        // captures its own snapshot at the same point and never presents a
        // frame it could not draw.
        if (!SarLayerPathRenderFrame(&snapshot)) {
            fprintf(stderr, "stage1: the GPU layer path failed to render frame %llu\n", frame_counter);
            Shutdown(1);
        }
        SarPresentGpuFrame(SarLayerPathSceneTexture(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else {
        // This matches the M1 headless order: handle the VBlank interrupt
        // first, then render the software PPU frame, then count the VBlank.
        gpsp_draw_frame(gameImage);
        SarPresentFrame(gameImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
    CaptureFrame();
    SarPresentSwap();
    Pace();

    if (exit_after_frame != 0 && frame_counter == exit_after_frame) {
        Shutdown(0);
    }

    frame_counter++;
}

static void InitVideo(void)
{
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;
    int capture_present;
    int exit_present;
    const char *path;

    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "stage0: SDL video init failed: %s\n", SDL_GetError());
        Shutdown(1);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    window = SDL_CreateWindow("Sonic Advance Remaster - M2 stage 0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              DISPLAY_WIDTH, DISPLAY_HEIGHT, window_flags);
    if (window == NULL) {
        fprintf(stderr, "stage0: window creation failed: %s\n", SDL_GetError());
        Shutdown(1);
    }

    gl_context = SDL_GL_CreateContext(window);
    if (gl_context == NULL) {
        fprintf(stderr, "stage0: GL 3.3 core context creation failed: %s\n", SDL_GetError());
        Shutdown(1);
    }

    if (SDL_GL_MakeCurrent(window, gl_context) < 0) {
        fprintf(stderr, "stage0: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        Shutdown(1);
    }

    {
        const char *vsync = getenv("SAR_VSYNC");
        int swap_interval = (vsync != NULL && strcmp(vsync, "0") == 0) ? 0 : 1;

        if (SDL_GL_SetSwapInterval(swap_interval) < 0) {
            fprintf(stderr, "stage0: vsync unavailable; pacing with the 60 Hz clock\n");
        } else if (swap_interval == 0) {
            printf("stage0: vsync disabled; pacing with the 60 Hz clock\n");
        }
    }

    if (!SarPresentInit(window)) {
        Shutdown(1);
    }

    {
        const char *renderer = getenv("SAR_RENDERER");

        if (renderer == NULL || *renderer == '\0' || strcmp(renderer, "software") == 0) {
            renderer_gl = 0;
        } else if (strcmp(renderer, "gl") == 0) {
            renderer_gl = 1;
        } else {
            fprintf(stderr, "stage0: SAR_RENDERER must be software or gl\n");
            Shutdown(2);
        }
    }

    if (renderer_gl) {
        if (!SarLayerPathInit(window)) {
            const char *fallback = getenv("SAR_ALLOW_SOFTWARE_FALLBACK");

            if (fallback != NULL && strcmp(fallback, "1") == 0) {
                fprintf(stderr, "stage1: the GPU layer path failed; falling back to the software PPU\n");
                renderer_gl = 0;
            } else {
                fprintf(stderr,
                        "stage1: the GPU layer path failed; set SAR_ALLOW_SOFTWARE_FALLBACK=1 to use the software PPU\n");
                Shutdown(1);
            }
        } else {
            printf("stage1: renderer=gl\n");
        }
    } else {
        printf("stage0: renderer=software\n");
    }

    tick_frequency = SDL_GetPerformanceFrequency();
    next_tick = SDL_GetPerformanceCounter();

    exit_after_frame = ReadFrameEnv("SAR_EXIT_AFTER_FRAME", &exit_present);
    if (!exit_present) {
        exit_after_frame = 0;
    }

    {
        const char *frames = getenv("SAR_CAPTURE_FRAME");

        if (frames != NULL && *frames != '\0') {
            capture_present = 1;
            ReadCaptureFrames(frames);
        }
    }

    if (capture_present) {
        capture_dir = getenv("SAR_CAPTURE_DIR");
        if (capture_dir == NULL || *capture_dir == '\0') {
            fprintf(stderr, "stage0: SAR_CAPTURE_FRAME needs SAR_CAPTURE_DIR\n");
            Shutdown(2);
        }
    }

    path = getenv("SAR_KEY_LOG");
    if (path != NULL && *path != '\0') {
        key_log = fopen(path, "w");
        if (key_log == NULL) {
            fprintf(stderr, "stage0: cannot open key log %s\n", path);
            Shutdown(2);
        }
    }
}

int main(void)
{
    printf("stage0: Sonic Advance Remaster host, software PPU presentation\n");
    printf("stage0: frame %dx%d, GL 3.3 core, 60 Hz tick\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    run_start = SDL_GetPerformanceCounter();

    SaveOpen();
    SarOamBridgeInit();
    SarScriptInit();
    InitVideo();
    AudioInit();

    REG_RCNT = 0x8000;
    REG_KEYINPUT = 0x3FF;

    AgbMain();

    Shutdown(0);
    return 0;
}
