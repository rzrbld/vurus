#pragma once
#include <string>
using GLdouble = double;
#define GL_QUADS 0x0007
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_ALPHA_TEST 0x0BC0
#define GL_CLIP_PLANE0 0x3000
#define GL_CLIP_PLANE1 0x3001
#define GL_SMOOTH 0x1D01
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_MODULATE 0x2100
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_ALPHA 0x8572
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE1_RGB 0x8581
#define GL_SOURCE0_ALPHA 0x8588
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND1_RGB 0x8591
#define GL_OPERAND0_ALPHA 0x8598
#define GL_PRIMARY_COLOR 0x8577
#define GL_CONSTANT 0x8576
#define GL_ADD 0x0104
#define GL_ENABLE_BIT 0x00002000
#define GL_CURRENT_BIT 0x00000001
namespace vyruz::gles {
bool initialize(std::string& error);
void shutdown();
// Deferred draws must complete before raw GL resource/state operations.
void flush();
void texImage2D(GLenum target,GLint level,GLint internalformat,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const void* pixels);
void texSubImage2D(GLenum target,GLint level,GLint xoffset,GLint yoffset,GLsizei width,GLsizei height,GLenum format,GLenum type,const void* pixels);
void texParameteri(GLenum target,GLenum pname,GLint param);
void generateMipmap(GLenum target);
void deleteTextures(GLsizei n,const GLuint* textures);
void deleteFramebuffers(GLsizei n,const GLuint* framebuffers);
void copyTexSubImage2D(GLenum target,GLint level,GLint xoffset,GLint yoffset,GLint x,GLint y,GLsizei width,GLsizei height);
void bindFramebuffer(GLenum target,GLuint framebuffer);
void framebufferTexture2D(GLenum target,GLenum attachment,GLenum textarget,GLuint texture,GLint level);
void readPixels(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,void* pixels);
void viewport(GLint x,GLint y,GLsizei width,GLsizei height);
void scissor(GLint x,GLint y,GLsizei width,GLsizei height);
void clear(GLbitfield mask);
void useProgram(GLuint program);
void blitFramebuffer(GLint srcX0,GLint srcY0,GLint srcX1,GLint srcY1,GLint dstX0,GLint dstY0,GLint dstX1,GLint dstY1,GLbitfield mask,GLenum filter);
void blendEquation(GLenum mode);
void blendEquationSeparate(GLenum modeRGB,GLenum modeAlpha);
void colorMask(GLboolean red,GLboolean green,GLboolean blue,GLboolean alpha);
void depthMask(GLboolean flag);
void depthFunc(GLenum func);
void cullFace(GLenum mode);
void frontFace(GLenum mode);
void activeTexture(GLenum texture);
void bindTexture(GLenum target,GLuint texture);
void blendFunc(GLenum src,GLenum dst);
void blendFuncSeparate(GLenum srcRGB,GLenum dstRGB,GLenum srcAlpha,GLenum dstAlpha);

void begin(GLenum);void end();
void vertex(float,float);void texCoord(float,float);void color(float,float,float,float);
void enable(GLenum);void disable(GLenum);
void matrixMode(GLenum);void loadIdentity();void pushMatrix();void popMatrix();
void ortho(double,double,double,double,double,double);
void translate(float,float,float);void scale(float,float,float);void rotate(float,float,float,float);
void alphaFunc(GLenum,float);void clipPlane(GLenum,const double*);
void texEnvi(GLenum,GLenum,GLint);void texEnvfv(GLenum,GLenum,const float*);
void pushAttrib(GLbitfield);void popAttrib();
void readTexture(int width,int height,void* pixels);
}
#define glBegin vyruz::gles::begin
#define glEnd vyruz::gles::end
#define glVertex2f vyruz::gles::vertex
#define glVertex2i vyruz::gles::vertex
#define glTexCoord2f vyruz::gles::texCoord
#define glColor4f vyruz::gles::color
#define glColor3f(r,g,b) vyruz::gles::color((r),(g),(b),1.f)
#define glColor3ub(r,g,b) vyruz::gles::color((r)/255.f,(g)/255.f,(b)/255.f,1.f)
#define glEnable vyruz::gles::enable
#define glDisable vyruz::gles::disable
#define glMatrixMode vyruz::gles::matrixMode
#define glLoadIdentity vyruz::gles::loadIdentity
#define glPushMatrix vyruz::gles::pushMatrix
#define glPopMatrix vyruz::gles::popMatrix
#define glOrtho vyruz::gles::ortho
#define glTranslatef vyruz::gles::translate
#define glScalef vyruz::gles::scale
#define glRotatef vyruz::gles::rotate
#define glAlphaFunc vyruz::gles::alphaFunc
#define glClipPlane vyruz::gles::clipPlane
#define glTexEnvi vyruz::gles::texEnvi
#define glTexEnvfv vyruz::gles::texEnvfv
#define glPushAttrib vyruz::gles::pushAttrib
#define glPopAttrib vyruz::gles::popAttrib
#define glShadeModel(mode) ((void)0)

#define glTexImage2D vyruz::gles::texImage2D
#define glTexSubImage2D vyruz::gles::texSubImage2D
#define glTexParameteri vyruz::gles::texParameteri
#define glGenerateMipmap vyruz::gles::generateMipmap
#define glDeleteTextures vyruz::gles::deleteTextures
#define glDeleteFramebuffers vyruz::gles::deleteFramebuffers
#define glCopyTexSubImage2D vyruz::gles::copyTexSubImage2D
#define glBindFramebuffer vyruz::gles::bindFramebuffer
#define glFramebufferTexture2D vyruz::gles::framebufferTexture2D
#define glReadPixels vyruz::gles::readPixels
#define glViewport vyruz::gles::viewport
#define glScissor vyruz::gles::scissor
#define glClear vyruz::gles::clear
#define glUseProgram vyruz::gles::useProgram
#define glBlitFramebuffer vyruz::gles::blitFramebuffer
#define glBlendEquation vyruz::gles::blendEquation
#define glBlendEquationSeparate vyruz::gles::blendEquationSeparate
#define glColorMask vyruz::gles::colorMask
#define glDepthMask vyruz::gles::depthMask
#define glDepthFunc vyruz::gles::depthFunc
#define glCullFace vyruz::gles::cullFace
#define glFrontFace vyruz::gles::frontFace
#define glActiveTexture vyruz::gles::activeTexture
#define glBindTexture vyruz::gles::bindTexture
#define glBlendFunc vyruz::gles::blendFunc
#define glBlendFuncSeparate vyruz::gles::blendFuncSeparate
