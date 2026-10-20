/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 0 presentation path. This is deliberately not a remaster renderer: it
 * converts the upstream software PPU's 16-bit ABGR1555 gameImage to RGBA8,
 * uploads it to a GL 3.3 core texture, and draws one textured quad with nearest
 * sampling. It exists to prove that the project-owned host can replace the
 * upstream SDL presentation loop while the game logic stays in the core
 * archive.
 */

#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "present.h"

struct sar_gl {
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*GenTextures)(GLsizei, GLuint *);
    void (*DeleteTextures)(GLsizei, const GLuint *);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    void (*PixelStorei)(GLenum, GLint);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    void (*ReadBuffer)(GLenum);
    void (*GenVertexArrays)(GLsizei, GLuint *);
    void (*BindVertexArray)(GLuint);
    void (*DeleteVertexArrays)(GLsizei, const GLuint *);
    void (*GenBuffers)(GLsizei, GLuint *);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void (*DeleteBuffers)(GLsizei, const GLuint *);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
    void (*EnableVertexAttribArray)(GLuint);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint *);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*DeleteShader)(GLuint);
    GLuint (*CreateProgram)(void);
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint *);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*DeleteProgram)(GLuint);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar *);
    void (*Uniform1i)(GLint, GLint);
    void (*ActiveTexture)(GLenum);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    GLenum (*GetError)(void);
    const GLubyte *(*GetString)(GLenum);
    void (*GetIntegerv)(GLenum, GLint *);
};

static struct sar_gl gl;
static SDL_Window *window;
static GLuint texture;
static GLuint program;
static GLuint vao;
static GLuint vbo;
static unsigned char *rgba;
static int texture_width;
static int texture_height;
static int viewport_x;
static int viewport_y;
static int viewport_width;
static int viewport_height;
static int initialized;
static int srgb_location = -1;

static int LoadProc(void *slot, const char *name)
{
    void *proc = SDL_GL_GetProcAddress(name);

    if (proc == NULL) {
        fprintf(stderr, "stage0: OpenGL entry point missing: %s\n", name);
        return 0;
    }

    memcpy(slot, &proc, sizeof(proc));
    return 1;
}

#define LOAD(field, name)                                                                                                                  \
    do {                                                                                                                                   \
        if (!LoadProc(&gl.field, name)) {                                                                                                  \
            return 0;                                                                                                                      \
        }                                                                                                                                  \
    } while (0)

static int LoadGL(void)
{
    LOAD(Viewport, "glViewport");
    LOAD(ClearColor, "glClearColor");
    LOAD(Clear, "glClear");
    LOAD(GenTextures, "glGenTextures");
    LOAD(DeleteTextures, "glDeleteTextures");
    LOAD(BindTexture, "glBindTexture");
    LOAD(TexParameteri, "glTexParameteri");
    LOAD(TexImage2D, "glTexImage2D");
    LOAD(TexSubImage2D, "glTexSubImage2D");
    LOAD(PixelStorei, "glPixelStorei");
    LOAD(ReadPixels, "glReadPixels");
    LOAD(ReadBuffer, "glReadBuffer");
    LOAD(GenVertexArrays, "glGenVertexArrays");
    LOAD(BindVertexArray, "glBindVertexArray");
    LOAD(DeleteVertexArrays, "glDeleteVertexArrays");
    LOAD(GenBuffers, "glGenBuffers");
    LOAD(BindBuffer, "glBindBuffer");
    LOAD(BufferData, "glBufferData");
    LOAD(DeleteBuffers, "glDeleteBuffers");
    LOAD(VertexAttribPointer, "glVertexAttribPointer");
    LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(CreateShader, "glCreateShader");
    LOAD(ShaderSource, "glShaderSource");
    LOAD(CompileShader, "glCompileShader");
    LOAD(GetShaderiv, "glGetShaderiv");
    LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
    LOAD(DeleteShader, "glDeleteShader");
    LOAD(CreateProgram, "glCreateProgram");
    LOAD(AttachShader, "glAttachShader");
    LOAD(LinkProgram, "glLinkProgram");
    LOAD(GetProgramiv, "glGetProgramiv");
    LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
    LOAD(DeleteProgram, "glDeleteProgram");
    LOAD(UseProgram, "glUseProgram");
    LOAD(GetUniformLocation, "glGetUniformLocation");
    LOAD(Uniform1i, "glUniform1i");
    LOAD(ActiveTexture, "glActiveTexture");
    LOAD(DrawArrays, "glDrawArrays");
    LOAD(GetError, "glGetError");
    LOAD(GetString, "glGetString");
    LOAD(GetIntegerv, "glGetIntegerv");

    return 1;
}

static GLuint CompileShader(GLenum type, const char *source)
{
    GLuint shader = gl.CreateShader(type);
    GLint ok = 0;
    GLint length = 0;
    char *log;

    if (shader == 0) {
        fprintf(stderr, "stage0: glCreateShader failed\n");
        return 0;
    }

    gl.ShaderSource(shader, 1, &source, NULL);
    gl.CompileShader(shader);
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        log = malloc((size_t)(length > 1 ? length : 1));
        if (log != NULL) {
            gl.GetShaderInfoLog(shader, length, NULL, log);
            fprintf(stderr, "stage0: shader compile failed: %s\n", log);
            free(log);
        }
        gl.DeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint BuildProgram(void)
{
    static const char *vertex_source = "#version 330 core\n"
                                       "layout(location = 0) in vec2 aPos;\n"
                                       "layout(location = 1) in vec2 aUV;\n"
                                       "out vec2 vUV;\n"
                                       "void main(void) {\n"
                                       "    vUV = aUV;\n"
                                       "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
                                       "}\n";
    static const char *fragment_source = "#version 330 core\n"
                                         "in vec2 vUV;\n"
                                         "out vec4 fragColor;\n"
                                         "uniform sampler2D uFrame;\n"
                                         "uniform int uSrgb;\n"
                                         "vec3 LinearToSrgb(vec3 c)\n"
                                         "{\n"
                                         "    vec3 low = c * 12.92;\n"
                                         "    vec3 high = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;\n"
                                         "    return mix(low, high, step(vec3(0.0031308), c));\n"
                                         "}\n"
                                         "void main(void) {\n"
                                         "    vec4 texel = texture(uFrame, vUV);\n"
                                         "    vec3 rgb = (uSrgb != 0) ? LinearToSrgb(texel.rgb) : texel.rgb;\n"
                                         "    fragColor = vec4(rgb, 1.0);\n"
                                         "}\n";
    GLuint vertex;
    GLuint fragment;
    GLuint result;
    GLint ok = 0;
    GLint length = 0;
    char *log;
    GLint location;

    vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
    if (vertex == 0) {
        return 0;
    }

    fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment == 0) {
        gl.DeleteShader(vertex);
        return 0;
    }

    result = gl.CreateProgram();
    gl.AttachShader(result, vertex);
    gl.AttachShader(result, fragment);
    gl.LinkProgram(result);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);

    gl.GetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) {
        gl.GetProgramiv(result, GL_INFO_LOG_LENGTH, &length);
        log = malloc((size_t)(length > 1 ? length : 1));
        if (log != NULL) {
            gl.GetProgramInfoLog(result, length, NULL, log);
            fprintf(stderr, "stage0: shader link failed: %s\n", log);
            free(log);
        }
        gl.DeleteProgram(result);
        return 0;
    }

    location = gl.GetUniformLocation(result, "uFrame");
    if (location < 0) {
        fprintf(stderr, "stage0: shader uniform uFrame is missing\n");
        gl.DeleteProgram(result);
        return 0;
    }

    srgb_location = gl.GetUniformLocation(result, "uSrgb");
    if (srgb_location < 0) {
        fprintf(stderr, "stage0: shader uniform uSrgb is missing\n");
        gl.DeleteProgram(result);
        return 0;
    }

    gl.UseProgram(result);
    gl.Uniform1i(location, 0);
    return result;
}

int SarPresentInit(SDL_Window *sdl_window)
{
    static const float vertices[] = {
        // position      // uv (v flipped: image row 0 is the top row)
        -1.0f, -1.0f, 0.0f, 1.0f,
        1.0f,  -1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f,  0.0f, 0.0f,
        1.0f,  1.0f,  1.0f, 0.0f,
    };
    GLint major = 0;
    GLint minor = 0;
    const GLubyte *version;
    const GLubyte *renderer;

    window = sdl_window;

    if (!LoadGL()) {
        return 0;
    }

    gl.GetIntegerv(GL_MAJOR_VERSION, &major);
    gl.GetIntegerv(GL_MINOR_VERSION, &minor);
    version = gl.GetString(GL_VERSION);
    renderer = gl.GetString(GL_RENDERER);
    printf("stage0: OpenGL %d.%d core, %s\n", major, minor, renderer != NULL ? (const char *)renderer : "unknown renderer");
    printf("stage0: GL_VERSION %s\n", version != NULL ? (const char *)version : "unknown");

    if (major < 3 || (major == 3 && minor < 3)) {
        fprintf(stderr, "stage0: OpenGL 3.3 core is required\n");
        return 0;
    }

    texture_width = DISPLAY_WIDTH;
    texture_height = DISPLAY_HEIGHT;
    rgba = malloc((size_t)texture_width * (size_t)texture_height * 4u);
    if (rgba == NULL) {
        fprintf(stderr, "stage0: out of memory for the RGBA upload buffer\n");
        return 0;
    }

    gl.GenTextures(1, &texture);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture_width, texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);

    program = BuildProgram();
    if (program == 0) {
        return 0;
    }

    gl.GenVertexArrays(1, &vao);
    gl.BindVertexArray(vao);
    gl.GenBuffers(1, &vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices, GL_STATIC_DRAW);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (const void *)0);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (const void *)(2 * sizeof(float)));
    gl.EnableVertexAttribArray(1);

    initialized = 1;
    return 1;
}

// Draws a texture over the whole drawable, with integer scaling and
// letterboxing. srgb converts the texture from linear light to sRGB.
static void PresentDraw(GLuint source, int width, int height, int srgb)
{
    int drawable_width = 0;
    int drawable_height = 0;
    int scale;

    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        return;
    }

    // Integer scale with letterboxing; fall back to a fractional fit if the
    // window is smaller than the native frame.
    scale = drawable_width / width;
    if (drawable_height / height < scale) {
        scale = drawable_height / height;
    }

    if (scale >= 1) {
        viewport_width = width * scale;
        viewport_height = height * scale;
    } else {
        double fit_x = (double)drawable_width / (double)width;
        double fit_y = (double)drawable_height / (double)height;
        double fit = fit_x < fit_y ? fit_x : fit_y;

        viewport_width = (int)((double)width * fit);
        viewport_height = (int)((double)height * fit);
        if (viewport_width < 1) {
            viewport_width = 1;
        }
        if (viewport_height < 1) {
            viewport_height = 1;
        }
    }

    viewport_x = (drawable_width - viewport_width) / 2;
    viewport_y = (drawable_height - viewport_height) / 2;

    gl.Viewport(0, 0, drawable_width, drawable_height);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);

    gl.Viewport(viewport_x, viewport_y, viewport_width, viewport_height);
    gl.UseProgram(program);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source);
    gl.Uniform1i(srgb_location, srgb);
    gl.BindVertexArray(vao);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void SarPresentFrame(const uint16_t *pixels, int width, int height)
{
    int i;

    if (!initialized) {
        return;
    }

    if (width != texture_width || height != texture_height) {
        fprintf(stderr, "stage0: unexpected frame size %dx%d (expected %dx%d)\n", width, height, texture_width, texture_height);
        return;
    }

    for (i = 0; i < width * height; i++) {
        uint16_t pixel = pixels[i];
        unsigned char *out = &rgba[i * 4];

        out[0] = (unsigned char)((pixel & 0x1Fu) << 3);
        out[1] = (unsigned char)(((pixel >> 5) & 0x1Fu) << 3);
        out[2] = (unsigned char)(((pixel >> 10) & 0x1Fu) << 3);
        out[3] = 255;
    }

    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    PresentDraw(texture, width, height, 0);
}

// Presents the composited scene of the GPU layer path. The scene target
// holds linear light, so the present shader encodes sRGB.
void SarPresentGpuFrame(unsigned int source, int width, int height)
{
    if (!initialized) {
        return;
    }

    if (width != texture_width || height != texture_height) {
        fprintf(stderr, "stage0: unexpected frame size %dx%d (expected %dx%d)\n", width, height, texture_width, texture_height);
        return;
    }

    PresentDraw((GLuint)source, width, height, 1);
}

int SarPresentCaptureGlPpm(const char *path)
{
    unsigned char *readback;
    FILE *file;
    int x;
    int y;

    if (!initialized || viewport_width <= 0 || viewport_height <= 0) {
        fprintf(stderr, "stage0: no framebuffer to capture\n");
        return 0;
    }

    readback = malloc((size_t)viewport_width * (size_t)viewport_height * 4u);
    if (readback == NULL) {
        fprintf(stderr, "stage0: out of memory for framebuffer readback\n");
        return 0;
    }

    gl.ReadBuffer(GL_BACK);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(viewport_x, viewport_y, viewport_width, viewport_height, GL_RGBA, GL_UNSIGNED_BYTE, readback);

    if (gl.GetError() != GL_NO_ERROR) {
        fprintf(stderr, "stage0: glReadPixels failed\n");
        free(readback);
        return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "stage0: cannot open %s for writing\n", path);
        free(readback);
        return 0;
    }

    fprintf(file, "P6\n%d %d\n255\n", viewport_width, viewport_height);
    for (y = viewport_height - 1; y >= 0; y--) {
        for (x = 0; x < viewport_width; x++) {
            const unsigned char *pixel = &readback[((size_t)y * (size_t)viewport_width + (size_t)x) * 4u];
            fputc(pixel[0], file);
            fputc(pixel[1], file);
            fputc(pixel[2], file);
        }
    }

    fclose(file);
    free(readback);
    return 1;
}

void SarPresentSwap(void)
{
    if (initialized) {
        SDL_GL_SwapWindow(window);
    }
}

void SarPresentShutdown(void)
{
    if (!initialized) {
        return;
    }

    gl.DeleteProgram(program);
    gl.DeleteTextures(1, &texture);
    gl.DeleteBuffers(1, &vbo);
    gl.DeleteVertexArrays(1, &vao);
    free(rgba);
    initialized = 0;
}
