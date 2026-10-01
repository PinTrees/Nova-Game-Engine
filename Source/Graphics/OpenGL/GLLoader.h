#pragma once
#include <GL/gl.h>
#include <cstddef>
#include <string>

// OpenGL 4.5 함수 로더 — 외부 로더(glad 등) 없이 엔진이 쓰는 함수만 직접 선언한다.
//  - GL 1.1 함수·상수는 Windows SDK 의 GL/gl.h (opengl32.dll 이 내보냄)
//  - 그 밖의 함수는 wglGetProcAddress 로 컨텍스트를 만든 뒤 GLLoader::Load 에서 채운다
//  - 새 함수가 필요하면 NOVA_GL_FUNCTIONS 에 한 줄 더하면 된다 (상수는 아래 #define)

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef unsigned long long GLuint64;
typedef void (APIENTRY* GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);

// ---- 상수 (GL 1.1 밖)
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_CLAMP_TO_BORDER                0x812D
#define GL_MIRRORED_REPEAT                0x8370
#define GL_MIRROR_CLAMP_TO_EDGE           0x8743
#define GL_TEXTURE_WRAP_R                 0x8072
#define GL_TEXTURE_3D                     0x806F
#define GL_TEXTURE_CUBE_MAP               0x8513
#define GL_TEXTURE_2D_ARRAY               0x8C1A
#define GL_TEXTURE_CUBE_MAP_ARRAY         0x9009
#define GL_TEXTURE_BASE_LEVEL             0x813C
#define GL_TEXTURE_MAX_LEVEL              0x813D
#define GL_TEXTURE_MIN_LOD                0x813A
#define GL_TEXTURE_MAX_LOD                0x813B
#define GL_TEXTURE_LOD_BIAS               0x8501
#define GL_TEXTURE_MAX_ANISOTROPY         0x84FE
#define GL_TEXTURE_COMPARE_MODE           0x884C
#define GL_TEXTURE_COMPARE_FUNC           0x884D
#define GL_COMPARE_REF_TO_TEXTURE         0x884E
#define GL_TEXTURE_CUBE_MAP_SEAMLESS      0x884F

#define GL_R8                             0x8229
#define GL_RG8                            0x822B
#define GL_RGBA8                          0x8058
#define GL_SRGB8_ALPHA8                   0x8C43
#define GL_RGB10_A2                       0x8059
#define GL_R16F                           0x822D
#define GL_RG16F                          0x822F
#define GL_RGBA16F                        0x881A
#define GL_R32F                           0x822E
#define GL_RG32F                          0x8230
#define GL_RGBA32F                        0x8814
#define GL_R11F_G11F_B10F                 0x8C3A
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_DEPTH_COMPONENT32F             0x8CAC
#define GL_DEPTH24_STENCIL8               0x88F0
#define GL_DEPTH32F_STENCIL8              0x8CAD
#define GL_RG                             0x8227
#define GL_DEPTH_STENCIL                  0x84F9
#define GL_HALF_FLOAT                     0x140B
#define GL_UNSIGNED_INT_24_8              0x84FA

#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_UNIFORM_BUFFER                 0x8A11
#define GL_SHADER_STORAGE_BUFFER          0x90D2
#define GL_DYNAMIC_STORAGE_BIT            0x0100

#define GL_FRAMEBUFFER                    0x8D40
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_STENCIL_ATTACHMENT             0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5

#define GL_VERTEX_SHADER                  0x8B31
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_GEOMETRY_SHADER                0x8DD9
#define GL_TESS_CONTROL_SHADER            0x8E88
#define GL_TESS_EVALUATION_SHADER         0x8E87
#define GL_COMPUTE_SHADER                 0x91B9
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84

#define GL_LOWER_LEFT                     0x8CA1
#define GL_UPPER_LEFT                     0x8CA2
#define GL_NEGATIVE_ONE_TO_ONE            0x935E
#define GL_ZERO_TO_ONE                    0x935F

#define GL_FUNC_ADD                       0x8006
#define GL_MIN                            0x8007
#define GL_MAX                            0x8008
#define GL_FUNC_SUBTRACT                  0x800A
#define GL_FUNC_REVERSE_SUBTRACT          0x800B
#define GL_CONSTANT_COLOR                 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR       0x8002
#define GL_INCR_WRAP                      0x8507
#define GL_DECR_WRAP                      0x8508

#define GL_DEPTH_CLAMP                    0x864F
#define GL_MULTISAMPLE                    0x809D
#define GL_SAMPLE_ALPHA_TO_COVERAGE       0x809E
#define GL_FRAMEBUFFER_SRGB               0x8DB9
#define GL_PATCHES                        0x000E

#define GL_DEBUG_OUTPUT                   0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242
#define GL_DEBUG_SEVERITY_HIGH            0x9146
#define GL_DEBUG_SEVERITY_MEDIUM          0x9147
#define GL_DEBUG_SEVERITY_LOW             0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B
#define GL_DONT_CARE                      0x1100

#define GL_MAJOR_VERSION                  0x821B
#define GL_MINOR_VERSION                  0x821C
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C

// WGL_ARB_create_context
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB         0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

// ---- 함수 목록: X(반환형, 이름, 매개변수)
#define NOVA_GL_FUNCTIONS(X) \
	X(void, glCreateBuffers, (GLsizei n, GLuint* buffers)) \
	X(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers)) \
	X(void, glNamedBufferStorage, (GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags)) \
	X(void, glNamedBufferSubData, (GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data)) \
	X(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer)) \
	X(void, glCreateTextures, (GLenum target, GLsizei n, GLuint* textures)) \
	X(void, glTextureStorage2D, (GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)) \
	X(void, glTextureStorage3D, (GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)) \
	X(void, glTextureSubImage2D, (GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels)) \
	X(void, glTextureSubImage3D, (GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels)) \
	X(void, glTextureParameteri, (GLuint texture, GLenum pname, GLint param)) \
	X(void, glBindTextureUnit, (GLuint unit, GLuint texture)) \
	X(void, glGetTextureImage, (GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels)) \
	X(void, glGenerateTextureMipmap, (GLuint texture)) \
	X(void, glCreateSamplers, (GLsizei n, GLuint* samplers)) \
	X(void, glDeleteSamplers, (GLsizei n, const GLuint* samplers)) \
	X(void, glSamplerParameteri, (GLuint sampler, GLenum pname, GLint param)) \
	X(void, glSamplerParameterf, (GLuint sampler, GLenum pname, GLfloat param)) \
	X(void, glSamplerParameterfv, (GLuint sampler, GLenum pname, const GLfloat* param)) \
	X(void, glBindSampler, (GLuint unit, GLuint sampler)) \
	X(void, glCreateFramebuffers, (GLsizei n, GLuint* framebuffers)) \
	X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers)) \
	X(void, glNamedFramebufferTexture, (GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)) \
	X(void, glNamedFramebufferTextureLayer, (GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer)) \
	X(void, glNamedFramebufferDrawBuffers, (GLuint framebuffer, GLsizei n, const GLenum* bufs)) \
	X(GLenum, glCheckNamedFramebufferStatus, (GLuint framebuffer, GLenum target)) \
	X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
	X(void, glClearNamedFramebufferfv, (GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat* value)) \
	X(void, glClearNamedFramebufferfi, (GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)) \
	X(void, glBlitNamedFramebuffer, (GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)) \
	X(void, glCreateVertexArrays, (GLsizei n, GLuint* arrays)) \
	X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays)) \
	X(void, glBindVertexArray, (GLuint array)) \
	X(void, glEnableVertexArrayAttrib, (GLuint vaobj, GLuint index)) \
	X(void, glVertexArrayAttribFormat, (GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)) \
	X(void, glVertexArrayAttribIFormat, (GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)) \
	X(void, glVertexArrayAttribBinding, (GLuint vaobj, GLuint attribindex, GLuint bindingindex)) \
	X(void, glVertexArrayBindingDivisor, (GLuint vaobj, GLuint bindingindex, GLuint divisor)) \
	X(void, glVertexArrayVertexBuffer, (GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)) \
	X(void, glVertexArrayElementBuffer, (GLuint vaobj, GLuint buffer)) \
	X(GLuint, glCreateShader, (GLenum type)) \
	X(void, glDeleteShader, (GLuint shader)) \
	X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
	X(void, glCompileShader, (GLuint shader)) \
	X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
	X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
	X(GLuint, glCreateProgram, (void)) \
	X(void, glDeleteProgram, (GLuint program)) \
	X(void, glAttachShader, (GLuint program, GLuint shader)) \
	X(void, glDetachShader, (GLuint program, GLuint shader)) \
	X(void, glLinkProgram, (GLuint program)) \
	X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
	X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
	X(void, glUseProgram, (GLuint program)) \
	X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name)) \
	X(void, glProgramUniform1i, (GLuint program, GLint location, GLint v0)) \
	X(void, glClipControl, (GLenum origin, GLenum depth)) \
	X(void, glBlendFuncSeparate, (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)) \
	X(void, glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha)) \
	X(void, glBlendColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)) \
	X(void, glStencilFuncSeparate, (GLenum face, GLenum func, GLint ref, GLuint mask)) \
	X(void, glStencilOpSeparate, (GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)) \
	X(void, glDrawElementsInstancedBaseVertexBaseInstance, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount, GLint basevertex, GLuint baseinstance)) \
	X(void, glDrawArraysInstancedBaseInstance, (GLenum mode, GLint first, GLsizei count, GLsizei instancecount, GLuint baseinstance)) \
	X(void, glDebugMessageCallback, (GLDEBUGPROC callback, const void* userParam)) \
	X(void, glDebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled))

#define NOVA_GL_DECLARE(ret, name, params) typedef ret (APIENTRY* PFN_##name) params; extern PFN_##name name;
NOVA_GL_FUNCTIONS(NOVA_GL_DECLARE)
#undef NOVA_GL_DECLARE

namespace GLLoader
{
	// 컨텍스트가 현재인 상태에서 부른다. 빠진 함수가 있으면 false + missing 에 이름
	bool Load(std::string& missing);
}
