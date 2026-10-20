/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1 GPU layer path.
 *
 * The renderer consumes a per-tick snapshot (see snapshot.h) and draws the
 * GBA layers into offscreen targets with OpenGL 3.3 core and GLSL 330:
 *
 *   - one pass per background layer (text or affine), using the per-scanline
 *     scroll and affine state captured from the HBlank sequence;
 *   - one pass for the objects, instanced from the OAM snapshot;
 *   - one composite pass that applies the GBA priority, colour effect, window
 *     and object transparency rules on integer 5-bit colour and writes linear
 *     float.
 *
 * The upstream software PPU is not called from this path. It stays the
 * reference and the fallback.
 *
 * Data flow
 * ---------
 *   VRAM       R8UI texture, 1024 bytes per row, uploaded per dirty page
 *   palettes  R16UI texture, 512 entries, uploaded when the hash changes
 *   OAM       RGBA32UI texture, 128 objects of three texels
 *   scanlines RGBA32UI texture, 240 lines of nine texels
 *
 * The VRAM upload is page based: every 1 KB page is hashed each tick and only
 * the pages that changed are uploaded. The game does not expose a VRAM write
 * hook, so hashing is how the dirty range is found.
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "gba/defines.h"
#include "gba/types.h"

#include "renderer/gl_api.h"
#include "renderer/layer_path.h"
#include "renderer/snapshot.h"
#include "shaders/stage1_layers.h"

#define SAR_SCREEN_WIDTH DISPLAY_WIDTH
#define SAR_SCREEN_HEIGHT DISPLAY_HEIGHT
#define SAR_VRAM_TEX_WIDTH 1024
#define SAR_VRAM_TEX_HEIGHT ((int)(VRAM_SIZE / SAR_VRAM_TEX_WIDTH))
#define SAR_LINE_TEX_WIDTH 9
#define SAR_OBJ_TEX_WIDTH 128
#define SAR_OBJ_TEX_HEIGHT 3
#define SAR_MAX_INSTANCES 128

// Object dimensions indexed by shape and size, matching the software PPU.
static const u8 s_obj_dim[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
};

struct SarLayerPath {
    struct SarGlApi gl;

    GLuint prog_bg;
    GLuint prog_obj;
    GLuint prog_composite;

    GLuint vao_fullscreen;
    GLuint vao_obj;
    GLuint vbo_obj;

    GLuint tex_vram;
    GLuint tex_pal;
    GLuint tex_line;
    GLuint tex_obj;
    GLuint tex_bg[4];
    GLuint tex_obj_color;
    GLuint tex_obj_win;
    GLuint tex_scene;

    GLuint fbo_bg[4];
    GLuint fbo_obj;
    GLuint fbo_obj_win;
    GLuint fbo_scene;

    GLuint query;
    int gpu_timing;
    int query_used;

    int u_bg_vram;
    int u_bg_pal;
    int u_bg_line;
    int u_bg_layer;
    int u_obj_data;
    int u_obj_vram;
    int u_obj_pal;
    int u_obj_line;
    int u_obj_pass;
    int u_obj_screen;
    int u_comp_bg[4];
    int u_comp_obj;
    int u_comp_objwin;
    int u_comp_line;
    int u_comp_backdrop;

    uint64_t vram_hash[SAR_VRAM_TEX_HEIGHT];
    uint8_t vram_valid[SAR_VRAM_TEX_HEIGHT];
    uint64_t pal_hash;
    uint8_t pal_copy[SAR_SNAP_PALETTE_ENTRIES * 2];

    uint32_t line_data[SAR_SCREEN_HEIGHT][SAR_LINE_TEX_WIDTH][4];
    uint32_t obj_data[SAR_OBJ_TEX_HEIGHT][SAR_OBJ_TEX_WIDTH][4];

    int obj_instances[SAR_MAX_INSTANCES];
    int obj_instance_count;
    int win_instances[SAR_MAX_INSTANCES];
    int win_instance_count;

    FILE *perf_log;
    FILE *dump_log;
    long dump_frame;
    FILE *vram_dump;
    long vram_dump_frame;
    FILE *oam_dump;
    long oam_dump_frame;
    FILE *pal_dump;
    long pal_dump_frame;
    char *layer_dump_prefix;
    long layer_dump_frame;

    uint64_t last_gpu_ns;
    int initialized;
};

static struct SarLayerPath s_path;

static uint64_t HashBytes(const uint8_t *data, size_t size)
{
    uint64_t hash = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }

    return hash;
}

static GLuint CompileShader(GLenum type, const char *source)
{
    GLuint shader = s_path.gl.CreateShader(type);
    GLint ok = 0;
    GLint length = 0;
    char *log;

    if (shader == 0) {
        fprintf(stderr, "stage1: glCreateShader failed\n");
        return 0;
    }

    s_path.gl.ShaderSource(shader, 1, &source, NULL);
    s_path.gl.CompileShader(shader);
    s_path.gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        s_path.gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        log = malloc((size_t)(length > 1 ? length : 1));
        if (log != NULL) {
            s_path.gl.GetShaderInfoLog(shader, length, NULL, log);
            fprintf(stderr, "stage1: shader compile failed: %s\n", log);
            free(log);
        }
        s_path.gl.DeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint BuildProgram(const char *vertex_source, const char *fragment_source)
{
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment;
    GLuint program;
    GLint ok = 0;
    GLint length = 0;
    char *log;

    if (vertex == 0) {
        return 0;
    }

    fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment == 0) {
        s_path.gl.DeleteShader(vertex);
        return 0;
    }

    program = s_path.gl.CreateProgram();
    s_path.gl.AttachShader(program, vertex);
    s_path.gl.AttachShader(program, fragment);
    s_path.gl.LinkProgram(program);
    s_path.gl.DeleteShader(vertex);
    s_path.gl.DeleteShader(fragment);

    s_path.gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        s_path.gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        log = malloc((size_t)(length > 1 ? length : 1));
        if (log != NULL) {
            s_path.gl.GetProgramInfoLog(program, length, NULL, log);
            fprintf(stderr, "stage1: shader link failed: %s\n", log);
            free(log);
        }
        s_path.gl.DeleteProgram(program);
        return 0;
    }

    return program;
}

static GLuint CreateTexture(GLenum internal_format, GLenum format, GLenum type, int width, int height)
{
    GLuint texture = 0;

    s_path.gl.GenTextures(1, &texture);
    s_path.gl.BindTexture(GL_TEXTURE_2D, texture);
    s_path.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s_path.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s_path.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s_path.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s_path.gl.TexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, width, height, 0, format, type, NULL);
    s_path.gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);

    return texture;
}

static int CreateTarget(GLuint *fbo, GLuint *texture, GLenum internal_format, GLenum format, GLenum type, int width, int height)
{
    GLenum status;

    *texture = CreateTexture(internal_format, format, type, width, height);
    if (*texture == 0) {
        return 0;
    }

    s_path.gl.GenFramebuffers(1, fbo);
    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    s_path.gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    status = s_path.gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "stage1: framebuffer incomplete (0x%04x)\n", (unsigned)status);
        return 0;
    }

    return 1;
}

static void BindTextureUnit(int unit, GLuint texture)
{
    s_path.gl.ActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    s_path.gl.BindTexture(GL_TEXTURE_2D, texture);
}

static int OpenDumpFile(const char *path, FILE **file, long *frame)
{
    const char *frame_env;

    *file = fopen(path, "wb");
    if (*file == NULL) {
        fprintf(stderr, "stage1: cannot open %s\n", path);
        return 0;
    }

    *frame = -1;
    frame_env = getenv("SAR_DUMP_FRAME");
    if (frame_env != NULL && *frame_env != '\0') {
        *frame = strtol(frame_env, NULL, 10);
    }

    return 1;
}

int SarLayerPathInit(SDL_Window *window)
{
    GLint major = 0;
    GLint minor = 0;
    const char *path;
    int i;

    (void)window;

    if (s_path.initialized) {
        return 1;
    }

    if (!SarGlLoad(&s_path.gl)) {
        fprintf(stderr, "stage1: the OpenGL 3.3 core entry points are not available\n");
        return 0;
    }

    s_path.gl.GetIntegerv(GL_MAJOR_VERSION, &major);
    s_path.gl.GetIntegerv(GL_MINOR_VERSION, &minor);
    printf("stage1: OpenGL %d.%d core, %s\n", major, minor, s_path.gl.GetString(GL_RENDERER));

    if (major < 3 || (major == 3 && minor < 3)) {
        fprintf(stderr, "stage1: OpenGL 3.3 core is required for the GPU layer path\n");
        return 0;
    }

    s_path.prog_bg = BuildProgram(SAR_GLSL_FULLSCREEN_VS, SAR_GLSL_BG_FS);
    s_path.prog_obj = BuildProgram(SAR_GLSL_OBJ_VS, SAR_GLSL_OBJ_FS);
    s_path.prog_composite = BuildProgram(SAR_GLSL_FULLSCREEN_VS, SAR_GLSL_COMPOSITE_FS);
    if (s_path.prog_bg == 0 || s_path.prog_obj == 0 || s_path.prog_composite == 0) {
        fprintf(stderr, "stage1: the Stage 1 shaders did not compile\n");
        return 0;
    }

    s_path.tex_vram = CreateTexture(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, SAR_VRAM_TEX_WIDTH, SAR_VRAM_TEX_HEIGHT);
    s_path.tex_pal = CreateTexture(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, SAR_SNAP_PALETTE_ENTRIES, 1);
    s_path.tex_line = CreateTexture(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, SAR_LINE_TEX_WIDTH, SAR_SCREEN_HEIGHT);
    s_path.tex_obj = CreateTexture(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, SAR_OBJ_TEX_WIDTH, SAR_OBJ_TEX_HEIGHT);

    for (i = 0; i < 4; i++) {
        if (!CreateTarget(&s_path.fbo_bg[i], &s_path.tex_bg[i], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SAR_SCREEN_WIDTH,
                          SAR_SCREEN_HEIGHT)) {
            fprintf(stderr, "stage1: the background layer target could not be created\n");
            return 0;
        }
    }

    if (!CreateTarget(&s_path.fbo_obj, &s_path.tex_obj_color, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SAR_SCREEN_WIDTH,
                      SAR_SCREEN_HEIGHT)) {
        fprintf(stderr, "stage1: the object target could not be created\n");
        return 0;
    }

    if (!CreateTarget(&s_path.fbo_obj_win, &s_path.tex_obj_win, GL_R8, GL_RED, GL_UNSIGNED_BYTE, SAR_SCREEN_WIDTH,
                      SAR_SCREEN_HEIGHT)) {
        fprintf(stderr, "stage1: the object window target could not be created\n");
        return 0;
    }

    if (!CreateTarget(&s_path.fbo_scene, &s_path.tex_scene, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, SAR_SCREEN_WIDTH,
                      SAR_SCREEN_HEIGHT)) {
        fprintf(stderr, "stage1: the scene target could not be created\n");
        return 0;
    }

    s_path.gl.GenVertexArrays(1, &s_path.vao_fullscreen);
    s_path.gl.BindVertexArray(s_path.vao_fullscreen);

    s_path.gl.GenVertexArrays(1, &s_path.vao_obj);
    s_path.gl.BindVertexArray(s_path.vao_obj);
    s_path.gl.GenBuffers(1, &s_path.vbo_obj);
    s_path.gl.BindBuffer(GL_ARRAY_BUFFER, s_path.vbo_obj);
    s_path.gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(s_path.obj_instances), s_path.obj_instances,
                         GL_DYNAMIC_DRAW);
    s_path.gl.EnableVertexAttribArray(0);
    s_path.gl.VertexAttribIPointer(0, 1, GL_INT, sizeof(int), (const void *)0);
    s_path.gl.VertexAttribDivisor(0, 1);
    s_path.gl.BindVertexArray(0);

    s_path.u_bg_vram = s_path.gl.GetUniformLocation(s_path.prog_bg, "uVram");
    s_path.u_bg_pal = s_path.gl.GetUniformLocation(s_path.prog_bg, "uPal");
    s_path.u_bg_line = s_path.gl.GetUniformLocation(s_path.prog_bg, "uLine");
    s_path.u_bg_layer = s_path.gl.GetUniformLocation(s_path.prog_bg, "uLayer");

    s_path.u_obj_data = s_path.gl.GetUniformLocation(s_path.prog_obj, "uObj");
    s_path.u_obj_vram = s_path.gl.GetUniformLocation(s_path.prog_obj, "uVram");
    s_path.u_obj_pal = s_path.gl.GetUniformLocation(s_path.prog_obj, "uPal");
    s_path.u_obj_line = s_path.gl.GetUniformLocation(s_path.prog_obj, "uLine");
    s_path.u_obj_pass = s_path.gl.GetUniformLocation(s_path.prog_obj, "uObjPass");
    s_path.u_obj_screen = s_path.gl.GetUniformLocation(s_path.prog_obj, "uScreen");

    s_path.u_comp_bg[0] = s_path.gl.GetUniformLocation(s_path.prog_composite, "uBg0");
    s_path.u_comp_bg[1] = s_path.gl.GetUniformLocation(s_path.prog_composite, "uBg1");
    s_path.u_comp_bg[2] = s_path.gl.GetUniformLocation(s_path.prog_composite, "uBg2");
    s_path.u_comp_bg[3] = s_path.gl.GetUniformLocation(s_path.prog_composite, "uBg3");
    s_path.u_comp_obj = s_path.gl.GetUniformLocation(s_path.prog_composite, "uObj");
    s_path.u_comp_objwin = s_path.gl.GetUniformLocation(s_path.prog_composite, "uObjWin");
    s_path.u_comp_line = s_path.gl.GetUniformLocation(s_path.prog_composite, "uLine");
    s_path.u_comp_backdrop = s_path.gl.GetUniformLocation(s_path.prog_composite, "uBackdrop");

    if (s_path.gl.GenQueries != NULL) {
        s_path.gl.GenQueries(1, &s_path.query);
        s_path.gpu_timing = 1;
        printf("stage1: GL timer queries enabled\n");
    } else {
        printf("stage1: GL timer queries are not available; GPU timing is disabled\n");
    }

    // Debug dumps. They are review aids, not part of the game path.
    path = getenv("SAR_SNAPSHOT_DUMP");
    if (path != NULL && *path != '\0') {
        if (!OpenDumpFile(path, &s_path.dump_log, &s_path.dump_frame)) {
            return 0;
        }
    }

    path = getenv("SAR_VRAM_DUMP");
    if (path != NULL && *path != '\0') {
        if (!OpenDumpFile(path, &s_path.vram_dump, &s_path.vram_dump_frame)) {
            return 0;
        }
    }

    path = getenv("SAR_OAM_DUMP");
    if (path != NULL && *path != '\0') {
        if (!OpenDumpFile(path, &s_path.oam_dump, &s_path.oam_dump_frame)) {
            return 0;
        }
    }

    path = getenv("SAR_PALETTE_DUMP");
    if (path != NULL && *path != '\0') {
        if (!OpenDumpFile(path, &s_path.pal_dump, &s_path.pal_dump_frame)) {
            return 0;
        }
    }

    path = getenv("SAR_LAYER_DUMP");
    if (path != NULL && *path != '\0') {
        s_path.layer_dump_prefix = strdup(path);
        s_path.layer_dump_frame = -1;
        path = getenv("SAR_DUMP_FRAME");
        if (path != NULL && *path != '\0') {
            s_path.layer_dump_frame = strtol(path, NULL, 10);
        }
    }

    path = getenv("SAR_PERF_LOG");
    if (path != NULL && *path != '\0') {
        s_path.perf_log = fopen(path, "w");
        if (s_path.perf_log == NULL) {
            fprintf(stderr, "stage1: cannot open the performance log %s\n", path);
            return 0;
        }
        fprintf(s_path.perf_log, "frame,capture_us,upload_us,render_us,gpu_us\n");
    }

    memset(s_path.vram_valid, 0, sizeof(s_path.vram_valid));
    s_path.pal_hash = 0;

    s_path.initialized = 1;
    printf("stage1: GPU layer path ready (%dx%d, %d VRAM pages)\n", SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT, SAR_VRAM_TEX_HEIGHT);
    return 1;
}

static void UploadVram(const SarSnapshot *snapshot)
{
    int page;

    for (page = 0; page < SAR_VRAM_TEX_HEIGHT; page++) {
        const uint8_t *data = snapshot->vram + (size_t)page * SAR_VRAM_TEX_WIDTH;
        uint64_t hash = HashBytes(data, SAR_VRAM_TEX_WIDTH);

        if (s_path.vram_valid[page] && hash == s_path.vram_hash[page]) {
            continue;
        }

        s_path.gl.BindTexture(GL_TEXTURE_2D, s_path.tex_vram);
        s_path.gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, page, SAR_VRAM_TEX_WIDTH, 1, GL_RED_INTEGER, GL_UNSIGNED_BYTE, data);
        s_path.vram_hash[page] = hash;
        s_path.vram_valid[page] = 1;
    }
}

static void UploadPalette(const SarSnapshot *snapshot)
{
    uint64_t hash = HashBytes((const uint8_t *)snapshot->palette, sizeof(s_path.pal_copy));

    if (hash == s_path.pal_hash) {
        return;
    }

    memcpy(s_path.pal_copy, snapshot->palette, sizeof(s_path.pal_copy));
    s_path.gl.BindTexture(GL_TEXTURE_2D, s_path.tex_pal);
    s_path.gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SAR_SNAP_PALETTE_ENTRIES, 1, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                           s_path.pal_copy);
    s_path.pal_hash = hash;
}

static void BuildLineTexture(const SarSnapshot *snapshot)
{
    int y;
    int b;

    for (y = 0; y < SAR_SCREEN_HEIGHT; y++) {
        const SarSnapshotLine *line = &snapshot->line[y];
        uint32_t (*row)[4] = s_path.line_data[y];

        for (b = 0; b < 4; b++) {
            row[0][b] = line->hofs[b];
            row[1][b] = line->vofs[b];
            row[2][b] = line->cnt[b];
        }

        row[3][0] = (uint16_t)line->pa[2];
        row[3][1] = (uint16_t)line->pb[2];
        row[3][2] = (uint16_t)line->pc[2];
        row[3][3] = (uint16_t)line->pd[2];
        row[4][0] = (uint16_t)line->pa[3];
        row[4][1] = (uint16_t)line->pb[3];
        row[4][2] = (uint16_t)line->pc[3];
        row[4][3] = (uint16_t)line->pd[3];

        row[5][0] = (uint32_t)line->aref_x[0];
        row[5][1] = (uint32_t)line->aref_y[0];
        row[5][2] = (uint32_t)line->aref_x[1];
        row[5][3] = (uint32_t)line->aref_y[1];

        row[6][0] = line->dispcnt;
        row[6][1] = line->bldcnt;
        row[6][2] = line->winin;
        row[6][3] = line->winout;

        row[7][0] = line->mosaic;
        row[7][1] = line->bldalpha;
        row[7][2] = line->bldy;
        row[7][3] = 0;

        row[8][0] = line->win0h;
        row[8][1] = line->win0v;
        row[8][2] = line->win1h;
        row[8][3] = line->win1v;
    }

    s_path.gl.BindTexture(GL_TEXTURE_2D, s_path.tex_line);
    s_path.gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SAR_LINE_TEX_WIDTH, SAR_SCREEN_HEIGHT, GL_RGBA_INTEGER,
                            GL_UNSIGNED_INT, s_path.line_data);
}

// Decodes the OAM snapshot into the object texture and builds the draw list.
// The list is ordered back to front: the highest priority number first, and
// inside one priority the highest OAM index first, so the front object wins.
static void BuildObjects(const SarSnapshot *snapshot)
{
    const OamData *oam = (const OamData *)snapshot->oam;
    int priority_order[4][128];
    int priority_count[4];
    int i;
    int p;

    for (p = 0; p < 4; p++) {
        priority_count[p] = 0;
    }

    memset(s_path.obj_data, 0, sizeof(s_path.obj_data));

    for (i = 0; i < 128; i++) {
        const OamData *entry = &oam[i];
        uint32_t flags = 0;
        uint32_t tile;
        uint32_t priority;
        uint32_t palette;
        int width;
        int height;
        int shape;
        int size;
        int mode;
        int matrix;

        shape = (int)entry->split.shape;
        size = (int)entry->split.size;
        mode = (int)entry->split.objMode;
        matrix = (int)entry->split.matrixNum;

        if (shape == 3 || mode == 3) {
            continue;
        }

        // Affine mode 2 disables the object.
        if ((entry->split.affineMode & 1u) == 0u && (entry->split.affineMode & 2u) != 0u) {
            continue;
        }

        width = s_obj_dim[shape][size][0];
        height = s_obj_dim[shape][size][1];

        if ((entry->split.affineMode & 2u) != 0u) {
            width *= 2;
            height *= 2;
        }

        if (!(entry->split.y + height > 0) || !(entry->split.y < SAR_SCREEN_HEIGHT)) {
            continue;
        }

        if (!(entry->split.x + width > 0) || !(entry->split.x < SAR_SCREEN_WIDTH)) {
            continue;
        }

        priority = entry->split.priority & 3u;
        tile = entry->split.tileNum & 0x3FFu;
        palette = entry->split.paletteNum & 0xFu;

        flags |= (entry->split.affineMode & 1u) ? 1u : 0u;
        flags |= (entry->split.affineMode & 2u) ? 2u : 0u;
        flags |= entry->split.bpp ? 4u : 0u;
        flags |= ((matrix >> 3) & 1) ? 8u : 0u;
        flags |= ((matrix >> 4) & 1) ? 16u : 0u;
        flags |= entry->split.mosaic ? 32u : 0u;
        flags |= (uint32_t)(mode & 3) << 8;

        s_path.obj_data[0][i][0] = (uint16_t)entry->split.x;
        s_path.obj_data[0][i][1] = (uint16_t)entry->split.y;
        s_path.obj_data[0][i][2] = flags;
        s_path.obj_data[0][i][3] = tile | (priority << 10) | (palette << 12);

        s_path.obj_data[1][i][0] = (uint32_t)s_obj_dim[shape][size][0];
        s_path.obj_data[1][i][1] = (uint32_t)s_obj_dim[shape][size][1];

        if (flags & 1u) {
            const OamData *group = &oam[matrix * 4];

            s_path.obj_data[1][i][2] = (uint16_t)group[0].all.affineParam;
            s_path.obj_data[1][i][3] = (uint16_t)group[1].all.affineParam;
            s_path.obj_data[2][i][0] = (uint16_t)group[2].all.affineParam;
            s_path.obj_data[2][i][1] = (uint16_t)group[3].all.affineParam;
        }

        priority_order[priority][priority_count[priority]++] = i;
    }

    s_path.obj_instance_count = 0;
    s_path.win_instance_count = 0;

    for (p = 3; p >= 0; p--) {
        int j;

        for (j = priority_count[p] - 1; j >= 0; j--) {
            int index = priority_order[p][j];
            uint32_t mode = (s_path.obj_data[0][index][2] >> 8) & 3u;

            if (mode == 2) {
                if (s_path.win_instance_count < SAR_MAX_INSTANCES) {
                    s_path.win_instances[s_path.win_instance_count++] = index;
                }
            } else if (s_path.obj_instance_count < SAR_MAX_INSTANCES) {
                s_path.obj_instances[s_path.obj_instance_count++] = index;
            }
        }
    }

    s_path.gl.BindTexture(GL_TEXTURE_2D, s_path.tex_obj);
    s_path.gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SAR_OBJ_TEX_WIDTH, SAR_OBJ_TEX_HEIGHT, GL_RGBA_INTEGER,
                            GL_UNSIGNED_INT, s_path.obj_data);
}

static void RenderBackgroundLayers(void)
{
    int layer;

    s_path.gl.UseProgram(s_path.prog_bg);
    BindTextureUnit(0, s_path.tex_vram);
    BindTextureUnit(1, s_path.tex_pal);
    BindTextureUnit(2, s_path.tex_line);
    s_path.gl.Uniform1i(s_path.u_bg_vram, 0);
    s_path.gl.Uniform1i(s_path.u_bg_pal, 1);
    s_path.gl.Uniform1i(s_path.u_bg_line, 2);

    s_path.gl.BindVertexArray(s_path.vao_fullscreen);

    for (layer = 0; layer < 4; layer++) {
        s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, s_path.fbo_bg[layer]);
        s_path.gl.Viewport(0, 0, SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT);
        s_path.gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        s_path.gl.Clear(GL_COLOR_BUFFER_BIT);
        s_path.gl.Uniform1i(s_path.u_bg_layer, layer);
        s_path.gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

static void DrawObjects(const int *instances, int count, int pass)
{
    if (count <= 0) {
        return;
    }

    s_path.gl.BindBuffer(GL_ARRAY_BUFFER, s_path.vbo_obj);
    s_path.gl.BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(count * (int)sizeof(int)), instances);

    s_path.gl.UseProgram(s_path.prog_obj);
    BindTextureUnit(0, s_path.tex_obj);
    BindTextureUnit(1, s_path.tex_vram);
    BindTextureUnit(2, s_path.tex_pal);
    BindTextureUnit(3, s_path.tex_line);
    s_path.gl.Uniform1i(s_path.u_obj_data, 0);
    s_path.gl.Uniform1i(s_path.u_obj_vram, 1);
    s_path.gl.Uniform1i(s_path.u_obj_pal, 2);
    s_path.gl.Uniform1i(s_path.u_obj_line, 3);
    s_path.gl.Uniform1i(s_path.u_obj_pass, pass);
    s_path.gl.Uniform2f(s_path.u_obj_screen, (float)SAR_SCREEN_WIDTH, (float)SAR_SCREEN_HEIGHT);
    s_path.gl.BindVertexArray(s_path.vao_obj);
    s_path.gl.DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
}

static void RenderObjects(void)
{
    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, s_path.fbo_obj);
    s_path.gl.Viewport(0, 0, SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT);
    s_path.gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    s_path.gl.Clear(GL_COLOR_BUFFER_BIT);
    DrawObjects(s_path.obj_instances, s_path.obj_instance_count, 0);

    // The object window mask is only used when the display control asks for
    // it, but it is cleared every frame so it never holds stale data.
    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, s_path.fbo_obj_win);
    s_path.gl.Viewport(0, 0, SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT);
    s_path.gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    s_path.gl.Clear(GL_COLOR_BUFFER_BIT);
    DrawObjects(s_path.win_instances, s_path.win_instance_count, 1);
}

static void RenderComposite(const SarSnapshot *snapshot)
{
    uint16_t backdrop = snapshot->palette[0];
    int i;

    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, s_path.fbo_scene);
    s_path.gl.Viewport(0, 0, SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT);
    s_path.gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    s_path.gl.Clear(GL_COLOR_BUFFER_BIT);

    s_path.gl.UseProgram(s_path.prog_composite);
    for (i = 0; i < 4; i++) {
        BindTextureUnit(i, s_path.tex_bg[i]);
        s_path.gl.Uniform1i(s_path.u_comp_bg[i], i);
    }
    BindTextureUnit(4, s_path.tex_obj_color);
    s_path.gl.Uniform1i(s_path.u_comp_obj, 4);
    BindTextureUnit(5, s_path.tex_obj_win);
    s_path.gl.Uniform1i(s_path.u_comp_objwin, 5);
    BindTextureUnit(6, s_path.tex_line);
    s_path.gl.Uniform1i(s_path.u_comp_line, 6);

    s_path.gl.Uniform3f(s_path.u_comp_backdrop, (float)(backdrop & 0x1F), (float)((backdrop >> 5) & 0x1F),
                        (float)((backdrop >> 10) & 0x1F));

    s_path.gl.BindVertexArray(s_path.vao_fullscreen);
    s_path.gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Leave the default framebuffer bound so the present pass draws to the
    // window and the readback path can read the back buffer.
    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void DumpLayerTexture(const char *path, GLuint fbo, int width, int height)
{
    unsigned char *pixels;
    FILE *file;
    int i;

    pixels = malloc((size_t)width * (size_t)height * 4u);
    if (pixels == NULL) {
        return;
    }

    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    s_path.gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    s_path.gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    s_path.gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

    file = fopen(path, "wb");
    if (file != NULL) {
        fprintf(file, "P6\n%d %d\n255\n", width, height);
        for (i = 0; i < width * height; i++) {
            fputc(pixels[i * 4], file);
            fputc(pixels[i * 4 + 1], file);
            fputc(pixels[i * 4 + 2], file);
        }
        fclose(file);
    }

    free(pixels);
}

static void DumpLayers(void)
{
    char path[1024];
    int i;

    for (i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s_bg%d.ppm", s_path.layer_dump_prefix, i);
        DumpLayerTexture(path, s_path.fbo_bg[i], SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT);
    }

    snprintf(path, sizeof(path), "%s_obj.ppm", s_path.layer_dump_prefix);
    DumpLayerTexture(path, s_path.fbo_obj, SAR_SCREEN_WIDTH, SAR_SCREEN_HEIGHT);
}

int SarLayerPathRenderFrame(SarSnapshot *snapshot)
{
    Uint64 capture_start;
    Uint64 upload_start;
    Uint64 render_start;
    Uint64 now;
    double capture_us;
    double upload_us;
    double render_us;

    if (!s_path.initialized) {
        return 0;
    }

    capture_start = SDL_GetPerformanceCounter();
    if (!SarSnapshotCapture(snapshot)) {
        fprintf(stderr, "stage1: the frame snapshot could not be captured\n");
        return 0;
    }
    now = SDL_GetPerformanceCounter();
    capture_us = (double)(now - capture_start) * 1000000.0 / (double)SDL_GetPerformanceFrequency();

    if (s_path.dump_log != NULL && (s_path.dump_frame < 0 || (long)snapshot->frame == s_path.dump_frame)) {
        SarSnapshotDump(snapshot, s_path.dump_log);
    }

    if (s_path.vram_dump != NULL
        && (s_path.vram_dump_frame < 0 || (long)snapshot->frame == s_path.vram_dump_frame)) {
        fwrite(snapshot->vram, 1, snapshot->vram_size, s_path.vram_dump);
        fflush(s_path.vram_dump);
    }

    if (s_path.oam_dump != NULL
        && (s_path.oam_dump_frame < 0 || (long)snapshot->frame == s_path.oam_dump_frame)) {
        fwrite(snapshot->oam, 1, sizeof(snapshot->oam), s_path.oam_dump);
        fflush(s_path.oam_dump);
    }

    if (s_path.pal_dump != NULL
        && (s_path.pal_dump_frame < 0 || (long)snapshot->frame == s_path.pal_dump_frame)) {
        fwrite(snapshot->palette, 1, sizeof(snapshot->palette), s_path.pal_dump);
        fflush(s_path.pal_dump);
    }

    upload_start = now;
    UploadVram(snapshot);
    UploadPalette(snapshot);
    BuildLineTexture(snapshot);
    BuildObjects(snapshot);
    now = SDL_GetPerformanceCounter();
    upload_us = (double)(now - upload_start) * 1000000.0 / (double)SDL_GetPerformanceFrequency();

    if (s_path.gpu_timing) {
        GLint available = 0;

        // A query object that was never used cannot be read back, so the
        // first frame only starts the timer.
        if (s_path.query_used) {
            s_path.gl.GetQueryObjectiv(s_path.query, GL_QUERY_RESULT_AVAILABLE, &available);
        }
        if (available) {
            uint64_t nanoseconds = 0;

            s_path.gl.GetQueryObjectui64v(s_path.query, GL_QUERY_RESULT, &nanoseconds);
            s_path.last_gpu_ns = nanoseconds;
        }
        s_path.gl.BeginQuery(GL_TIME_ELAPSED, s_path.query);
    }

    s_path.gl.Disable(GL_BLEND);
    s_path.gl.Disable(GL_DEPTH_TEST);

    render_start = SDL_GetPerformanceCounter();
    RenderBackgroundLayers();
    RenderObjects();
    RenderComposite(snapshot);
    now = SDL_GetPerformanceCounter();
    render_us = (double)(now - render_start) * 1000000.0 / (double)SDL_GetPerformanceFrequency();

    if (s_path.gpu_timing) {
        s_path.gl.EndQuery(GL_TIME_ELAPSED);
        s_path.query_used = 1;
    }

    if (!SarGlCheckErrors(&s_path.gl, "the stage 1 frame")) {
        return 0;
    }

    if (s_path.layer_dump_prefix != NULL
        && (s_path.layer_dump_frame < 0 || (long)snapshot->frame == s_path.layer_dump_frame)) {
        DumpLayers();
    }

    if (s_path.perf_log != NULL) {
        fprintf(s_path.perf_log, "%u,%.1f,%.1f,%.1f,%.1f\n", snapshot->frame, capture_us, upload_us, render_us,
                (double)s_path.last_gpu_ns / 1000.0);
        fflush(s_path.perf_log);
    }

    return 1;
}

unsigned int SarLayerPathSceneTexture(void)
{
    return (unsigned int)s_path.tex_scene;
}

int SarLayerPathHasGpuTiming(void)
{
    return s_path.gpu_timing;
}

void SarLayerPathShutdown(void)
{
    if (!s_path.initialized) {
        return;
    }

    if (s_path.perf_log != NULL) {
        fclose(s_path.perf_log);
        s_path.perf_log = NULL;
    }

    if (s_path.dump_log != NULL) {
        fclose(s_path.dump_log);
        s_path.dump_log = NULL;
    }

    if (s_path.vram_dump != NULL) {
        fclose(s_path.vram_dump);
        s_path.vram_dump = NULL;
    }

    if (s_path.oam_dump != NULL) {
        fclose(s_path.oam_dump);
        s_path.oam_dump = NULL;
    }

    if (s_path.pal_dump != NULL) {
        fclose(s_path.pal_dump);
        s_path.pal_dump = NULL;
    }

    if (s_path.layer_dump_prefix != NULL) {
        free(s_path.layer_dump_prefix);
        s_path.layer_dump_prefix = NULL;
    }

    if (s_path.gpu_timing) {
        s_path.gl.DeleteQueries(1, &s_path.query);
    }

    s_path.gl.DeleteFramebuffers(4, s_path.fbo_bg);
    s_path.gl.DeleteFramebuffers(1, &s_path.fbo_obj);
    s_path.gl.DeleteFramebuffers(1, &s_path.fbo_obj_win);
    s_path.gl.DeleteFramebuffers(1, &s_path.fbo_scene);
    s_path.gl.DeleteTextures(1, &s_path.tex_vram);
    s_path.gl.DeleteTextures(1, &s_path.tex_pal);
    s_path.gl.DeleteTextures(1, &s_path.tex_line);
    s_path.gl.DeleteTextures(1, &s_path.tex_obj);
    s_path.gl.DeleteTextures(4, s_path.tex_bg);
    s_path.gl.DeleteTextures(1, &s_path.tex_obj_color);
    s_path.gl.DeleteTextures(1, &s_path.tex_obj_win);
    s_path.gl.DeleteTextures(1, &s_path.tex_scene);
    s_path.gl.DeleteBuffers(1, &s_path.vbo_obj);
    s_path.gl.DeleteVertexArrays(1, &s_path.vao_obj);
    s_path.gl.DeleteVertexArrays(1, &s_path.vao_fullscreen);
    s_path.gl.DeleteProgram(s_path.prog_bg);
    s_path.gl.DeleteProgram(s_path.prog_obj);
    s_path.gl.DeleteProgram(s_path.prog_composite);

    s_path.initialized = 0;
}
