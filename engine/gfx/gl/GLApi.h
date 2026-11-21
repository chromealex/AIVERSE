// OpenGL entry points used by the GL backend.
//  * Android / Linux (EGL) / Web (Emscripten): link GLES3 directly.
//  * Windows: desktop GL 3.3 core, functions above GL 1.1 are loaded at runtime via
//    wglGetProcAddress (no GLEW/GLAD dependency).
#pragma once
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <GL/gl.h>
  #include <cstddef>
  #define AIVERSE_GL_LOADER 1
  typedef char GLchar;
  typedef ptrdiff_t GLsizeiptr;
  typedef ptrdiff_t GLintptr;
  #define GL_ARRAY_BUFFER 0x8892
  #define GL_ELEMENT_ARRAY_BUFFER 0x8893
  #define GL_UNIFORM_BUFFER 0x8A11
  #define GL_STATIC_DRAW 0x88E4
  #define GL_DYNAMIC_DRAW 0x88E8
  #define GL_STREAM_DRAW 0x88E0
  #define GL_FRAGMENT_SHADER 0x8B30
  #define GL_VERTEX_SHADER 0x8B31
  #define GL_COMPILE_STATUS 0x8B81
  #define GL_LINK_STATUS 0x8B82
  #define GL_INFO_LOG_LENGTH 0x8B84
  #define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
  #define GL_INVALID_INDEX 0xFFFFFFFFu
  #define GL_TEXTURE0 0x84C0
  #define GL_CLAMP_TO_EDGE 0x812F
  #define GL_R8 0x8229
  #define GL_RG8 0x822B
  #define GL_RG 0x8227
  #define GL_R16F 0x822D
  #define GL_RG16F 0x822F
  #define GL_R32F 0x822E
  #define GL_RG32F 0x8230
  #define GL_RED 0x1903
  #define GL_RGBA8 0x8058
  #define GL_FUNC_ADD 0x8006
  #define GL_FUNC_SUBTRACT 0x800A
  #define GL_FUNC_REVERSE_SUBTRACT 0x800B
  #define GL_FRAMEBUFFER_SRGB 0x8DB9
  #define GL_SRGB8_ALPHA8 0x8C43
  #define GL_READ_FRAMEBUFFER 0x8CA8
  #define GL_DRAW_FRAMEBUFFER 0x8CA9
  #define GL_DEPTH_COMPONENT24 0x81A6
  #define GL_RGBA32F 0x8814
  #define GL_EXTENSIONS 0x1F03
  #define GL_NUM_EXTENSIONS 0x821D
  #define GL_TEXTURE_MAX_LEVEL 0x813D
  #define GL_TEXTURE_3D 0x806F
  #define GL_TEXTURE_2D_ARRAY 0x8C1A
  #define GL_TEXTURE_CUBE_MAP 0x8513
  #define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
  #define GL_TEXTURE_WRAP_R 0x8072
  #define GL_MIRRORED_REPEAT 0x8370
  #define GL_CLAMP_TO_BORDER 0x812D
  #define GL_TEXTURE_MIN_LOD 0x813A
  #define GL_TEXTURE_MAX_LOD 0x813B
  #define GL_TEXTURE_LOD_BIAS 0x8501
  #define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
  #define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
  #define GL_TEXTURE_COMPARE_MODE 0x884C
  #define GL_TEXTURE_COMPARE_FUNC 0x884D
  #define GL_COMPARE_REF_TO_TEXTURE 0x884E
  #define GL_FRAMEBUFFER 0x8D40
  #define GL_COLOR_ATTACHMENT0 0x8CE0
  #define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
  #define GL_MAX_DRAW_BUFFERS 0x8824
  #define GL_COLOR 0x1800
  #define GL_DEPTH 0x1801
  #define GL_DEPTH_ATTACHMENT 0x8D00
  #define GL_FRAMEBUFFER_COMPLETE 0x8CD5
  #define GL_PIXEL_PACK_BUFFER 0x88EB
  #define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
  #define GL_STREAM_READ 0x88E1
  #define GL_MAP_READ_BIT 0x0001
  #define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
  #define GL_ALREADY_SIGNALED 0x911A
  #define GL_TIMEOUT_EXPIRED 0x911B
  #define GL_CONDITION_SATISFIED 0x911C
  #define GL_WAIT_FAILED 0x911D
  struct __GLsync;
  typedef struct __GLsync* GLsync;
  #define GL_RENDERBUFFER 0x8D41
  #define GL_MAX_SAMPLES 0x8D57
  #define GL_TIME_ELAPSED 0x88BF
  #define GL_QUERY_RESULT 0x8866
  #define GL_QUERY_RESULT_AVAILABLE 0x8867
  typedef unsigned long long GLuint64;
  typedef struct __GLsync* GLsync;
  #define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
  #define GL_ALREADY_SIGNALED 0x911A
  #define GL_TIMEOUT_EXPIRED 0x911B
  #define GL_CONDITION_SATISFIED 0x911C
  #define GL_WAIT_FAILED 0x911D
  #define AIVERSE_GL_FUNCS(X) \
    X(GLsync, glFenceSync, (GLenum condition, GLbitfield flags)) \
    X(GLenum, glClientWaitSync, (GLsync sync, GLbitfield flags, GLuint64 timeout)) \
    X(void, glDeleteSync, (GLsync sync)) \
    X(void, glGenQueries, (GLsizei n, GLuint* ids)) \
    X(void, glDeleteQueries, (GLsizei n, const GLuint* ids)) \
    X(void, glBeginQuery, (GLenum target, GLuint id)) \
    X(void, glEndQuery, (GLenum target)) \
    X(void, glGetQueryObjectuiv, (GLuint id, GLenum pname, GLuint* params)) \
    X(void, glGetQueryObjectui64v, (GLuint id, GLenum pname, GLuint64* params)) \
    X(const GLubyte*, glGetStringi, (GLenum name, GLuint index)) \
    X(void, glGenBuffers, (GLsizei n, GLuint* b)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* b)) \
    X(void, glBindBuffer, (GLenum t, GLuint b)) \
    X(void, glBufferData, (GLenum t, GLsizeiptr s, const void* d, GLenum u)) \
    X(void, glBufferSubData, (GLenum t, GLintptr o, GLsizeiptr s, const void* d)) \
    X(void, glBindBufferRange, (GLenum t, GLuint i, GLuint b, GLintptr o, GLsizeiptr s)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* a)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* a)) \
    X(void, glBindVertexArray, (GLuint a)) \
    X(void, glEnableVertexAttribArray, (GLuint i)) \
    X(void, glDisableVertexAttribArray, (GLuint i)) \
    X(void, glVertexAttribPointer, (GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void* p)) \
    X(GLuint, glCreateShader, (GLenum t)) \
    X(void, glDeleteShader, (GLuint s)) \
    X(void, glShaderSource, (GLuint s, GLsizei c, const GLchar* const* str, const GLint* len)) \
    X(void, glCompileShader, (GLuint s)) \
    X(void, glGetShaderiv, (GLuint s, GLenum p, GLint* v)) \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei m, GLsizei* l, GLchar* log)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glDeleteProgram, (GLuint p)) \
    X(void, glAttachShader, (GLuint p, GLuint s)) \
    X(void, glBindAttribLocation, (GLuint p, GLuint i, const GLchar* n)) \
    X(void, glLinkProgram, (GLuint p)) \
    X(void, glGetProgramiv, (GLuint p, GLenum e, GLint* v)) \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei m, GLsizei* l, GLchar* log)) \
    X(void, glUseProgram, (GLuint p)) \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar* n)) \
    X(void, glUniform1i, (GLint l, GLint v)) \
    X(GLuint, glGetUniformBlockIndex, (GLuint p, const GLchar* n)) \
    X(void, glUniformBlockBinding, (GLuint p, GLuint i, GLuint b)) \
    X(void, glActiveTexture, (GLenum t)) \
    X(void, glGenerateMipmap, (GLenum t)) \
    X(void, glGenSamplers, (GLsizei count, GLuint* samplers)) \
    X(void, glDeleteSamplers, (GLsizei count, const GLuint* samplers)) \
    X(void, glBindSampler, (GLuint unit, GLuint sampler)) \
    X(void, glSamplerParameteri, (GLuint sampler, GLenum pname, GLint value)) \
    X(void, glSamplerParameterf, (GLuint sampler, GLenum pname, GLfloat value)) \
    X(void, glCompressedTexImage2D, (GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei s, const void* d)) \
    X(void, glTexImage3D, (GLenum t, GLint l, GLint f, GLsizei w, GLsizei h, GLsizei depth, GLint border, GLenum fmt, GLenum type, const void* data)) \
    X(void, glTexSubImage3D, (GLenum t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei depth, GLenum fmt, GLenum type, const void* data)) \
    X(void, glCompressedTexImage3D, (GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLsizei depth, GLint border, GLsizei size, const void* data)) \
    X(void, glDrawElementsInstanced, (GLenum m, GLsizei c, GLenum t, const void* i, GLsizei n)) \
    X(void, glBlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d)) \
    X(void, glBlendEquation, (GLenum m)) \
    X(void, glBlendEquationSeparate, (GLenum rgb, GLenum alpha)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint* f)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* f)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint* r)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* r)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint r)) \
    X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum format, GLsizei w, GLsizei h)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbufferTarget, GLuint r)) \
    X(void, glBindFramebuffer, (GLenum t, GLuint f)) \
    X(void, glFramebufferTexture2D, (GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l)) \
    X(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum t)) \
    X(void, glDrawBuffers, (GLsizei n, const GLenum* b)) \
    X(void, glClearBufferfv, (GLenum buffer, GLint drawbuffer, const GLfloat* value)) \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield m, GLenum f)) \
    X(GLsync, glFenceSync, (GLenum condition, GLbitfield flags)) \
    X(GLenum, glClientWaitSync, (GLsync sync, GLbitfield flags, GLuint64 timeout)) \
    X(void, glDeleteSync, (GLsync sync)) \
    X(void*, glMapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)) \
    X(GLboolean, glUnmapBuffer, (GLenum target))
  #define AIVERSE_GL_DECLARE(ret, name, args) typedef ret (APIENTRY* PFN_##name) args; extern PFN_##name name;
  AIVERSE_GL_FUNCS(AIVERSE_GL_DECLARE)
  #undef AIVERSE_GL_DECLARE
  namespace aiverse::gfx { bool loadGLFunctions(); }
#elif defined(__APPLE__)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
#elif defined(__EMSCRIPTEN__)
  #include <GLES3/gl3.h>
  // WebGL2 getBufferSubData (Emscripten library_webgl2.js); GLES3 headers omit it.
  extern "C" void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data);
#else
  #include <GLES3/gl3.h>
#endif
