/*
 * Minimal OpenGL 3.3 / ES 3.0 function loader using SDL_GL_GetProcAddress.
 * No external dependencies — just the GL functions SDL3D needs.
 */

#ifndef SDL3D_GL_FUNCS_H
#define SDL3D_GL_FUNCS_H

#include <SDL3/SDL.h>

/* GL types. */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef signed long long GLsizeiptr;
typedef unsigned int GLbitfield;

/* GL constants. */
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_RENDERBUFFER 0x8D41
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_RED 0x1903
#define GL_R32F 0x822E
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#define GL_NONE 0
#define GL_FRONT 0x0404
#define GL_NO_ERROR 0
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_RGBA16F 0x881A
#define GL_HALF_FLOAT 0x140B
#define GL_RGB 0x1907
#define GL_RG 0x8227
#define GL_RGB16F 0x881B
#define GL_RG16F 0x822F

/* Function pointer types. */
typedef void (*PFNGLCLEARPROC)(GLbitfield);
typedef void (*PFNGLCLEARCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLENABLEPROC)(GLenum);
typedef void (*PFNGLDISABLEPROC)(GLenum);
typedef void (*PFNGLDEPTHFUNCPROC)(GLenum);
typedef void (*PFNGLCULLFACEPROC)(GLenum);
typedef void (*PFNGLFRONTFACEPROC)(GLenum);
typedef void (*PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFNGLBLENDFUNCPROC)(GLenum, GLenum);
typedef void (*PFNGLSCISSORPROC)(GLint, GLint, GLsizei, GLsizei);
typedef GLenum (*PFNGLGETERRORPROC)(void);
typedef void (*PFNGLFLUSHPROC)(void);
typedef void (*PFNGLFINISHPROC)(void);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar **, const GLint *);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef void (*PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (*PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void (*PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void (*PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLGENBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (*PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
typedef void (*PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const void *);
typedef void (*PFNGLGENTEXTURESPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDTEXTUREPROC)(GLenum, GLuint);
typedef void (*PFNGLTEXIMAGE2DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (*PFNGLTEXIMAGE3DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void *);
typedef void (*PFNGLFRAMEBUFFERTEXTURELAYERPROC)(GLenum, GLenum, GLuint, GLint, GLint);
typedef void (*PFNGLTEXPARAMETERIPROC)(GLenum, GLenum, GLint);
typedef void (*PFNGLTEXPARAMETERFVPROC)(GLenum, GLenum, const GLfloat *);
typedef void (*PFNGLDELETETEXTURESPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLGENRENDERBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDRENDERBUFFERPROC)(GLenum, GLuint);
typedef void (*PFNGLRENDERBUFFERSTORAGEPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void (*PFNGLDELETERENDERBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLBLITFRAMEBUFFERPROC)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef GLint (*PFNGLGETATTRIBLOCATIONPROC)(GLuint, const GLchar *);
typedef void (*PFNGLREADPIXELSPROC)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void (*PFNGLTEXSUBIMAGE2DPROC)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (*PFNGLBINDBUFFERBASEPROC)(GLenum, GLuint, GLuint);
typedef void (*PFNGLUNIFORMBLOCKBINDINGPROC)(GLuint, GLuint, GLuint);
typedef GLuint (*PFNGLGETUNIFORMBLOCKINDEXPROC)(GLuint, const char *);
typedef void (*PFNGLDRAWBUFFERPROC)(GLenum);
typedef void (*PFNGLREADBUFFERPROC)(GLenum);
typedef void (*PFNGLUNIFORM1FVPROC)(GLint, GLsizei, const GLfloat *);
typedef void (*PFNGLGENERATEMIPMAPPROC)(GLenum);
/* Global function pointers. */
typedef struct sdl3d_gl_funcs
{
    PFNGLCLEARPROC Clear;
    PFNGLCLEARCOLORPROC ClearColor;
    PFNGLENABLEPROC Enable;
    PFNGLDISABLEPROC Disable;
    PFNGLDEPTHFUNCPROC DepthFunc;
    PFNGLCULLFACEPROC CullFace;
    PFNGLFRONTFACEPROC FrontFace;
    PFNGLVIEWPORTPROC Viewport;
    PFNGLBLENDFUNCPROC BlendFunc;
    PFNGLSCISSORPROC Scissor;
    PFNGLGETERRORPROC GetError;
    PFNGLFLUSHPROC Flush;
    PFNGLFINISHPROC Finish;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM2FPROC Uniform2f;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLUNIFORMMATRIX3FVPROC UniformMatrix3fv;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLDRAWARRAYSPROC DrawArrays;
    PFNGLDRAWELEMENTSPROC DrawElements;
    PFNGLGENTEXTURESPROC GenTextures;
    PFNGLBINDTEXTUREPROC BindTexture;
    PFNGLTEXIMAGE2DPROC TexImage2D;
    PFNGLTEXIMAGE3DPROC TexImage3D;
    PFNGLFRAMEBUFFERTEXTURELAYERPROC FramebufferTextureLayer;
    PFNGLTEXPARAMETERIPROC TexParameteri;
    PFNGLTEXPARAMETERFVPROC TexParameterfv;
    PFNGLDELETETEXTURESPROC DeleteTextures;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
    PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
    PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
    PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
    PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;
    PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer;
    PFNGLGETATTRIBLOCATIONPROC GetAttribLocation;
    PFNGLREADPIXELSPROC ReadPixels;
    PFNGLTEXSUBIMAGE2DPROC TexSubImage2D;
    PFNGLBINDBUFFERBASEPROC BindBufferBase;
    PFNGLUNIFORMBLOCKBINDINGPROC UniformBlockBinding;
    PFNGLGETUNIFORMBLOCKINDEXPROC GetUniformBlockIndex;
    PFNGLDRAWBUFFERPROC DrawBuffer;
    PFNGLREADBUFFERPROC ReadBuffer;
    PFNGLUNIFORM1FVPROC Uniform1fv;
    PFNGLGENERATEMIPMAPPROC GenerateMipmap;
} sdl3d_gl_funcs;

static bool sdl3d_gl_load_funcs(sdl3d_gl_funcs *gl)
{
#define LOAD(name)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        SDL_FunctionPointer fp = SDL_GL_GetProcAddress("gl" #name);                                                    \
        if (fp == NULL)                                                                                                \
        {                                                                                                              \
            return false;                                                                                              \
        }                                                                                                              \
        SDL_memcpy(&gl->name, &fp, sizeof(fp));                                                                        \
    } while (0)
    LOAD(Clear);
    LOAD(ClearColor);
    LOAD(Enable);
    LOAD(Disable);
    LOAD(DepthFunc);
    LOAD(CullFace);
    LOAD(FrontFace);
    LOAD(Viewport);
    LOAD(BlendFunc);
    LOAD(Scissor);
    LOAD(GetError);
    LOAD(Flush);
    LOAD(Finish);
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(DeleteShader);
    LOAD(CreateProgram);
    LOAD(AttachShader);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(UseProgram);
    LOAD(DeleteProgram);
    LOAD(GetUniformLocation);
    LOAD(Uniform1i);
    LOAD(Uniform1f);
    LOAD(Uniform2f);
    LOAD(Uniform3f);
    LOAD(Uniform4f);
    LOAD(UniformMatrix3fv);
    LOAD(UniformMatrix4fv);
    LOAD(GenVertexArrays);
    LOAD(BindVertexArray);
    LOAD(DeleteVertexArrays);
    LOAD(GenBuffers);
    LOAD(BindBuffer);
    LOAD(BufferData);
    LOAD(DeleteBuffers);
    LOAD(EnableVertexAttribArray);
    LOAD(VertexAttribPointer);
    LOAD(DrawArrays);
    LOAD(DrawElements);
    LOAD(GenTextures);
    LOAD(BindTexture);
    LOAD(TexImage2D);
    LOAD(TexImage3D);
    LOAD(FramebufferTextureLayer);
    LOAD(TexParameteri);
    LOAD(TexParameterfv);
    LOAD(DeleteTextures);
    LOAD(ActiveTexture);
    LOAD(GenFramebuffers);
    LOAD(BindFramebuffer);
    LOAD(FramebufferTexture2D);
    LOAD(CheckFramebufferStatus);
    LOAD(DeleteFramebuffers);
    LOAD(GenRenderbuffers);
    LOAD(BindRenderbuffer);
    LOAD(RenderbufferStorage);
    LOAD(FramebufferRenderbuffer);
    LOAD(DeleteRenderbuffers);
    LOAD(BlitFramebuffer);
    LOAD(GetAttribLocation);
    LOAD(ReadPixels);
    LOAD(TexSubImage2D);
    LOAD(BindBufferBase);
    LOAD(UniformBlockBinding);
    LOAD(GetUniformBlockIndex);
    LOAD(DrawBuffer);
    LOAD(ReadBuffer);
    LOAD(Uniform1fv);
    LOAD(GenerateMipmap);
#undef LOAD
    return true;
}

#endif
