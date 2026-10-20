/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Loads the OpenGL 3.3 core entry points used by the Stage 1 layer path
 * through SDL_GL_GetProcAddress. The host does not link libGL directly, so a
 * missing entry point is a startup error rather than a link error.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "gl_api.h"

int SarGlLoad(struct SarGlApi *api)
{
    void *proc;

#define LOAD(field, name)                                                                                                                  \
    do {                                                                                                                                   \
        proc = SDL_GL_GetProcAddress(name);                                                                                               \
        if (proc == NULL) {                                                                                                                \
            fprintf(stderr, "stage1: OpenGL entry point missing: %s\n", name);                                                            \
            return 0;                                                                                                                      \
        }                                                                                                                                  \
        memcpy(&api->field, &proc, sizeof(proc));                                                                                         \
    } while (0)

    LOAD(Viewport, "glViewport");
    LOAD(ClearColor, "glClearColor");
    LOAD(Clear, "glClear");
    LOAD(Enable, "glEnable");
    LOAD(Disable, "glDisable");
    LOAD(BlendFunc, "glBlendFunc");

    LOAD(GenTextures, "glGenTextures");
    LOAD(DeleteTextures, "glDeleteTextures");
    LOAD(BindTexture, "glBindTexture");
    LOAD(ActiveTexture, "glActiveTexture");
    LOAD(TexParameteri, "glTexParameteri");
    LOAD(TexImage2D, "glTexImage2D");
    LOAD(TexSubImage2D, "glTexSubImage2D");
    LOAD(PixelStorei, "glPixelStorei");
    LOAD(ReadPixels, "glReadPixels");

    LOAD(GenFramebuffers, "glGenFramebuffers");
    LOAD(BindFramebuffer, "glBindFramebuffer");
    LOAD(FramebufferTexture2D, "glFramebufferTexture2D");
    LOAD(CheckFramebufferStatus, "glCheckFramebufferStatus");
    LOAD(DeleteFramebuffers, "glDeleteFramebuffers");

    LOAD(GenVertexArrays, "glGenVertexArrays");
    LOAD(BindVertexArray, "glBindVertexArray");
    LOAD(DeleteVertexArrays, "glDeleteVertexArrays");
    LOAD(GenBuffers, "glGenBuffers");
    LOAD(BindBuffer, "glBindBuffer");
    LOAD(BufferData, "glBufferData");
    LOAD(BufferSubData, "glBufferSubData");
    LOAD(DeleteBuffers, "glDeleteBuffers");
    LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(VertexAttribIPointer, "glVertexAttribIPointer");
    LOAD(VertexAttribDivisor, "glVertexAttribDivisor");

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
    LOAD(Uniform1f, "glUniform1f");
    LOAD(Uniform2f, "glUniform2f");
    LOAD(Uniform3f, "glUniform3f");

    LOAD(DrawArrays, "glDrawArrays");
    LOAD(DrawArraysInstanced, "glDrawArraysInstanced");

    LOAD(GetError, "glGetError");
    LOAD(GetString, "glGetString");
    LOAD(GetIntegerv, "glGetIntegerv");

    // Timer queries are diagnostics. A driver without them still renders.
    proc = SDL_GL_GetProcAddress("glGenQueries");
    if (proc != NULL) {
        memcpy(&api->GenQueries, &proc, sizeof(proc));
        proc = SDL_GL_GetProcAddress("glDeleteQueries");
        memcpy(&api->DeleteQueries, &proc, sizeof(proc));
        proc = SDL_GL_GetProcAddress("glBeginQuery");
        memcpy(&api->BeginQuery, &proc, sizeof(proc));
        proc = SDL_GL_GetProcAddress("glEndQuery");
        memcpy(&api->EndQuery, &proc, sizeof(proc));
        proc = SDL_GL_GetProcAddress("glGetQueryObjectiv");
        memcpy(&api->GetQueryObjectiv, &proc, sizeof(proc));
        proc = SDL_GL_GetProcAddress("glGetQueryObjectui64v");
        if (proc == NULL) {
            proc = SDL_GL_GetProcAddress("glGetQueryObjectuiv");
        }
        memcpy(&api->GetQueryObjectui64v, &proc, sizeof(proc));
        if (api->GenQueries == NULL || api->DeleteQueries == NULL || api->BeginQuery == NULL
            || api->EndQuery == NULL || api->GetQueryObjectiv == NULL || api->GetQueryObjectui64v == NULL) {
            api->GenQueries = NULL;
            api->DeleteQueries = NULL;
            api->BeginQuery = NULL;
            api->EndQuery = NULL;
            api->GetQueryObjectiv = NULL;
            api->GetQueryObjectui64v = NULL;
        }
    }

#undef LOAD

    return 1;
}

int SarGlCheckErrors(const struct SarGlApi *api, const char *context)
{
    GLenum error = api->GetError();

    if (error == GL_NO_ERROR) {
        return 1;
    }

    fprintf(stderr, "stage1: GL error 0x%04x after %s\n", (unsigned)error, context);
    return 0;
}
