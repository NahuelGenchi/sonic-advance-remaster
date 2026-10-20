/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Project-owned OpenGL 3.3 core entry-point table for the Stage 1 layer path.
 *
 * The stage 0 present path has its own small table; the layer path needs a
 * larger surface (framebuffer objects, integer textures, instanced draws,
 * timer queries). Entry points are resolved through SDL_GL_GetProcAddress, so
 * the host does not link libGL directly.
 */

#ifndef SAR_GL_API_H
#define SAR_GL_API_H

#include <SDL_opengl.h>

struct SarGlApi {
    // State and framebuffer
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFunc)(GLenum, GLenum);

    // Textures
    void (*GenTextures)(GLsizei, GLuint *);
    void (*DeleteTextures)(GLsizei, const GLuint *);
    void (*BindTexture)(GLenum, GLuint);
    void (*ActiveTexture)(GLenum);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    void (*PixelStorei)(GLenum, GLint);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);

    // Framebuffer objects
    void (*GenFramebuffers)(GLsizei, GLuint *);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*DeleteFramebuffers)(GLsizei, const GLuint *);

    // Geometry
    void (*GenVertexArrays)(GLsizei, GLuint *);
    void (*BindVertexArray)(GLuint);
    void (*DeleteVertexArrays)(GLsizei, const GLuint *);
    void (*GenBuffers)(GLsizei, GLuint *);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
    void (*DeleteBuffers)(GLsizei, const GLuint *);
    void (*EnableVertexAttribArray)(GLuint);
    void (*VertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void *);
    void (*VertexAttribDivisor)(GLuint, GLuint);

    // Shaders
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
    void (*Uniform1f)(GLint, GLfloat);
    void (*Uniform2f)(GLint, GLfloat, GLfloat);
    void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);

    // Draw
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);

    // Misc
    GLenum (*GetError)(void);
    const GLubyte *(*GetString)(GLenum);
    void (*GetIntegerv)(GLenum, GLint *);

    // Timer queries (GL 3.3 core). The layer path disables GPU timing when a
    // pointer is missing; query timing is diagnostics, not a requirement.
    void (*GenQueries)(GLsizei, GLuint *);
    void (*DeleteQueries)(GLsizei, const GLuint *);
    void (*BeginQuery)(GLenum, GLuint);
    void (*EndQuery)(GLenum);
    void (*GetQueryObjectiv)(GLuint, GLenum, GLint *);
    void (*GetQueryObjectui64v)(GLuint, GLenum, GLuint64 *);
};

// Fills the table. Returns 0 and reports the first missing entry point.
int SarGlLoad(struct SarGlApi *api);

// Consumes the pending GL error state and reports whether it was clean.
int SarGlCheckErrors(const struct SarGlApi *api, const char *context);

#endif // SAR_GL_API_H
