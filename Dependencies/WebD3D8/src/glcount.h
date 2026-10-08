/*
**	Command & Conquer Generals Zero Hour(tm)
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
** WebAssembly port: per-frame counters of the WebGL calls the device makes.
**
** Every WebGL call crosses from WebAssembly into JavaScript, which is where the
** time of a Direct3D 8 layer goes, so the number of calls per frame is the
** metric that is tuned (and, with -webd3d8report, reported). The counting is a
** plain increment of a global per call; define WEBD3D8_NO_GL_COUNTERS to remove
** it.
**
** Include after <GLES3/gl3.h>: the wrappers are macros that increment the
** counter of the call's category and then call the real function.
*/
#pragma once

#include <cstdint>

namespace webd3d8 {

enum GLCategory
{
    GLC_DRAW,    ///< glDrawElements/glDrawArrays/glClear
    GLC_STATE,   ///< enables, blend, depth, stencil, viewport, pixel store...
    GLC_BIND,    ///< texture/buffer/framebuffer/program/vertex array/sampler binds, attachments
    GLC_UNIFORM, ///< glUniform*
    GLC_UPLOAD,  ///< buffer and texture data, texture/sampler parameters, storage
    GLC_ATTRIB,  ///< vertex attribute pointers and enables
    GLC_OTHER,   ///< object creation/deletion, queries
    GLC_COUNT
};

/// Counters of the current frame (reset by Present). Plain globals so that the
/// call wrappers cost one increment.
struct GLCounters
{
    uint32_t calls[GLC_COUNT];
    uint32_t total;
    uint32_t sync;        ///< calls that wait for the GPU (glReadPixels, glGet*, glCheckFramebufferStatus)
    uint32_t uploadBytes; ///< bytes handed to glBufferData/SubData and glTex(Sub)Image
};
extern GLCounters g_gl;

} // namespace webd3d8

#ifndef WEBD3D8_NO_GL_COUNTERS

#define WD3D_GLC(cat, fn) (::webd3d8::g_gl.calls[::webd3d8::cat]++, ::webd3d8::g_gl.total++, fn)
#define WD3D_GLS(fn) (::webd3d8::g_gl.sync++, ::webd3d8::g_gl.total++, ::webd3d8::g_gl.calls[::webd3d8::GLC_OTHER]++, fn)

#define glDrawElements(...) WD3D_GLC(GLC_DRAW, glDrawElements)(__VA_ARGS__)
#define glDrawArrays(...) WD3D_GLC(GLC_DRAW, glDrawArrays)(__VA_ARGS__)
#define glClear(...) WD3D_GLC(GLC_DRAW, glClear)(__VA_ARGS__)

#define glEnable(...) WD3D_GLC(GLC_STATE, glEnable)(__VA_ARGS__)
#define glDisable(...) WD3D_GLC(GLC_STATE, glDisable)(__VA_ARGS__)
#define glBlendFunc(...) WD3D_GLC(GLC_STATE, glBlendFunc)(__VA_ARGS__)
#define glBlendFuncSeparate(...) WD3D_GLC(GLC_STATE, glBlendFuncSeparate)(__VA_ARGS__)
#define glBlendEquation(...) WD3D_GLC(GLC_STATE, glBlendEquation)(__VA_ARGS__)
#define glDepthFunc(...) WD3D_GLC(GLC_STATE, glDepthFunc)(__VA_ARGS__)
#define glDepthMask(...) WD3D_GLC(GLC_STATE, glDepthMask)(__VA_ARGS__)
#define glDepthRangef(...) WD3D_GLC(GLC_STATE, glDepthRangef)(__VA_ARGS__)
#define glCullFace(...) WD3D_GLC(GLC_STATE, glCullFace)(__VA_ARGS__)
#define glFrontFace(...) WD3D_GLC(GLC_STATE, glFrontFace)(__VA_ARGS__)
#define glStencilFunc(...) WD3D_GLC(GLC_STATE, glStencilFunc)(__VA_ARGS__)
#define glStencilOp(...) WD3D_GLC(GLC_STATE, glStencilOp)(__VA_ARGS__)
#define glStencilMask(...) WD3D_GLC(GLC_STATE, glStencilMask)(__VA_ARGS__)
#define glColorMask(...) WD3D_GLC(GLC_STATE, glColorMask)(__VA_ARGS__)
#define glPolygonOffset(...) WD3D_GLC(GLC_STATE, glPolygonOffset)(__VA_ARGS__)
#define glViewport(...) WD3D_GLC(GLC_STATE, glViewport)(__VA_ARGS__)
#define glScissor(...) WD3D_GLC(GLC_STATE, glScissor)(__VA_ARGS__)
#define glClearColor(...) WD3D_GLC(GLC_STATE, glClearColor)(__VA_ARGS__)
#define glClearDepthf(...) WD3D_GLC(GLC_STATE, glClearDepthf)(__VA_ARGS__)
#define glClearStencil(...) WD3D_GLC(GLC_STATE, glClearStencil)(__VA_ARGS__)
#define glPixelStorei(...) WD3D_GLC(GLC_STATE, glPixelStorei)(__VA_ARGS__)

#define glBindTexture(...) WD3D_GLC(GLC_BIND, glBindTexture)(__VA_ARGS__)
#define glActiveTexture(...) WD3D_GLC(GLC_BIND, glActiveTexture)(__VA_ARGS__)
#define glBindSampler(...) WD3D_GLC(GLC_BIND, glBindSampler)(__VA_ARGS__)
#define glBindBuffer(...) WD3D_GLC(GLC_BIND, glBindBuffer)(__VA_ARGS__)
#define glBindFramebuffer(...) WD3D_GLC(GLC_BIND, glBindFramebuffer)(__VA_ARGS__)
#define glBindRenderbuffer(...) WD3D_GLC(GLC_BIND, glBindRenderbuffer)(__VA_ARGS__)
#define glBindVertexArray(...) WD3D_GLC(GLC_BIND, glBindVertexArray)(__VA_ARGS__)
#define glUseProgram(...) WD3D_GLC(GLC_BIND, glUseProgram)(__VA_ARGS__)
#define glFramebufferTexture2D(...) WD3D_GLC(GLC_BIND, glFramebufferTexture2D)(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) WD3D_GLC(GLC_BIND, glFramebufferRenderbuffer)(__VA_ARGS__)
#define glReadBuffer(...) WD3D_GLC(GLC_BIND, glReadBuffer)(__VA_ARGS__)
#define glDrawBuffers(...) WD3D_GLC(GLC_BIND, glDrawBuffers)(__VA_ARGS__)
#define glDrawElementsInstancedBaseVertexBaseInstanceWEBGL(...) WD3D_GLC(GLC_DRAW, glDrawElementsInstancedBaseVertexBaseInstanceWEBGL)(__VA_ARGS__)
#define glBlitFramebuffer(...) WD3D_GLC(GLC_DRAW, glBlitFramebuffer)(__VA_ARGS__)
#define glInvalidateFramebuffer(...) WD3D_GLC(GLC_BIND, glInvalidateFramebuffer)(__VA_ARGS__)

#define glUniform1i(...) WD3D_GLC(GLC_UNIFORM, glUniform1i)(__VA_ARGS__)
#define glUniform1f(...) WD3D_GLC(GLC_UNIFORM, glUniform1f)(__VA_ARGS__)
#define glUniform1fv(...) WD3D_GLC(GLC_UNIFORM, glUniform1fv)(__VA_ARGS__)
#define glUniform2f(...) WD3D_GLC(GLC_UNIFORM, glUniform2f)(__VA_ARGS__)
#define glUniform2fv(...) WD3D_GLC(GLC_UNIFORM, glUniform2fv)(__VA_ARGS__)
#define glUniform3f(...) WD3D_GLC(GLC_UNIFORM, glUniform3f)(__VA_ARGS__)
#define glUniform3fv(...) WD3D_GLC(GLC_UNIFORM, glUniform3fv)(__VA_ARGS__)
#define glUniform4f(...) WD3D_GLC(GLC_UNIFORM, glUniform4f)(__VA_ARGS__)
#define glUniform4fv(...) WD3D_GLC(GLC_UNIFORM, glUniform4fv)(__VA_ARGS__)
#define glUniformMatrix3fv(...) WD3D_GLC(GLC_UNIFORM, glUniformMatrix3fv)(__VA_ARGS__)
#define glUniformMatrix4fv(...) WD3D_GLC(GLC_UNIFORM, glUniformMatrix4fv)(__VA_ARGS__)
#define glBindBufferBase(...) WD3D_GLC(GLC_BIND, glBindBufferBase)(__VA_ARGS__)
#define glUniformBlockBinding(...) WD3D_GLC(GLC_UNIFORM, glUniformBlockBinding)(__VA_ARGS__)

#define glBufferData(...) WD3D_GLC(GLC_UPLOAD, glBufferData)(__VA_ARGS__)
#define glBufferSubData(...) WD3D_GLC(GLC_UPLOAD, glBufferSubData)(__VA_ARGS__)
#define glTexImage2D(...) WD3D_GLC(GLC_UPLOAD, glTexImage2D)(__VA_ARGS__)
#define glTexImage3D(...) WD3D_GLC(GLC_UPLOAD, glTexImage3D)(__VA_ARGS__)
#define glTexSubImage2D(...) WD3D_GLC(GLC_UPLOAD, glTexSubImage2D)(__VA_ARGS__)
#define glTexSubImage3D(...) WD3D_GLC(GLC_UPLOAD, glTexSubImage3D)(__VA_ARGS__)
#define glCompressedTexSubImage2D(...) WD3D_GLC(GLC_UPLOAD, glCompressedTexSubImage2D)(__VA_ARGS__)
#define glCompressedTexSubImage3D(...) WD3D_GLC(GLC_UPLOAD, glCompressedTexSubImage3D)(__VA_ARGS__)
#define glTexStorage2D(...) WD3D_GLC(GLC_UPLOAD, glTexStorage2D)(__VA_ARGS__)
#define glTexStorage3D(...) WD3D_GLC(GLC_UPLOAD, glTexStorage3D)(__VA_ARGS__)
#define glTexParameteri(...) WD3D_GLC(GLC_UPLOAD, glTexParameteri)(__VA_ARGS__)
#define glSamplerParameteri(...) WD3D_GLC(GLC_UPLOAD, glSamplerParameteri)(__VA_ARGS__)
#define glSamplerParameterf(...) WD3D_GLC(GLC_UPLOAD, glSamplerParameterf)(__VA_ARGS__)
#define glSamplerParameterfv(...) WD3D_GLC(GLC_UPLOAD, glSamplerParameterfv)(__VA_ARGS__)
#define glRenderbufferStorage(...) WD3D_GLC(GLC_UPLOAD, glRenderbufferStorage)(__VA_ARGS__)
#define glRenderbufferStorageMultisample(...) WD3D_GLC(GLC_UPLOAD, glRenderbufferStorageMultisample)(__VA_ARGS__)
#define glMapBufferRange(...) WD3D_GLC(GLC_UPLOAD, glMapBufferRange)(__VA_ARGS__)
#define glUnmapBuffer(...) WD3D_GLC(GLC_UPLOAD, glUnmapBuffer)(__VA_ARGS__)
#define glCopyBufferSubData(...) WD3D_GLC(GLC_UPLOAD, glCopyBufferSubData)(__VA_ARGS__)

#define glVertexAttribPointer(...) WD3D_GLC(GLC_ATTRIB, glVertexAttribPointer)(__VA_ARGS__)
#define glVertexAttribIPointer(...) WD3D_GLC(GLC_ATTRIB, glVertexAttribIPointer)(__VA_ARGS__)
#define glEnableVertexAttribArray(...) WD3D_GLC(GLC_ATTRIB, glEnableVertexAttribArray)(__VA_ARGS__)
#define glDisableVertexAttribArray(...) WD3D_GLC(GLC_ATTRIB, glDisableVertexAttribArray)(__VA_ARGS__)
#define glVertexAttrib4f(...) WD3D_GLC(GLC_ATTRIB, glVertexAttrib4f)(__VA_ARGS__)

#define glReadPixels(...) WD3D_GLS(glReadPixels)(__VA_ARGS__)
#define glCheckFramebufferStatus(...) WD3D_GLS(glCheckFramebufferStatus)(__VA_ARGS__)
#define glGetError(...) WD3D_GLS(glGetError)(__VA_ARGS__)

#endif // WEBD3D8_NO_GL_COUNTERS
