#include "render/gl_api.h"
#undef glEnable
#undef glDisable
#undef glTexImage2D
#undef glTexSubImage2D
#undef glTexParameteri
#undef glGenerateMipmap
#undef glDeleteTextures
#undef glDeleteFramebuffers
#undef glCopyTexSubImage2D
#undef glBindFramebuffer
#undef glFramebufferTexture2D
#undef glReadPixels
#undef glViewport
#undef glScissor
#undef glClear
#undef glUseProgram
#undef glBlitFramebuffer
#undef glBlendEquation
#undef glBlendEquationSeparate
#undef glColorMask
#undef glDepthMask
#undef glDepthFunc
#undef glCullFace
#undef glFrontFace
#undef glActiveTexture
#undef glBindTexture
#undef glBlendFunc
#undef glBlendFuncSeparate
#include <array>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstddef>
namespace vyruz::gles {
namespace {void driverEnable(GLenum);void driverDisable(GLenum);}
namespace {
using Matrix=std::array<float,16>;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix multiply(const Matrix&a,const Matrix&b){Matrix r{};for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;}
std::vector<Matrix> model{identity()},projection{identity()};
GLenum mode=GL_MODELVIEW,primitive=GL_QUADS;
auto& stack(){return mode==GL_PROJECTION?projection:model;}
struct State{bool texture=true,alpha=false;std::array<bool,2> clip{};float threshold=.5f;std::array<float,4> color{1,1,1,1},constant{};std::array<std::array<float,4>,2> planes{};GLint env=GL_MODULATE,combine=GL_ADD;};
State state;
// Uniforms belong to this private program and survive draw calls. Most quads
// share them; avoid sending identical values through the handheld driver.
State uploadedState;
Matrix uploadedModel{},uploadedProjection{};
bool uniformsValid=false;
struct Saved{State state;GLboolean blend,depth,cull;GLint src,dst;};
std::vector<Saved> saved;
struct Vertex{float x,y,u,v,r,g,b,a;};
std::vector<Vertex> vertices,triangles;
float u=0,v=0;
GLuint program=0,buffer=0,vao=0;
GLint transformLoc,modelLoc,textureLoc,alphaLoc,envLoc,constantLoc,planesLoc,clipLoc;

// Preserve submission order; only adjacent commands may merge. Tint is per vertex.
struct Command {State state;Matrix model,projection;GLenum primitive;GLuint texture;bool blend;std::array<GLint,4> factors;size_t first,count;};
std::vector<Vertex> queued;std::vector<Command> commands;
GLuint logicalTexture=0;GLenum logicalActiveTexture=GL_TEXTURE0;bool logicalBlend=false;std::array<GLint,4> logicalFactors{};
constexpr size_t kMaxQueuedVertices=65536,kMaxQueuedCommands=4096;
bool sameUniforms(const State&a,const State&b){return a.texture==b.texture&&a.alpha==b.alpha&&a.threshold==b.threshold&&a.clip==b.clip&&a.planes==b.planes&&a.env==b.env&&a.combine==b.combine&&a.constant==b.constant;}
bool compatible(const Command&a,const Command&b){
 // Incomplete primitives are discarded at each end, never joined across ends.
 const size_t width=a.primitive==GL_TRIANGLES?3:a.primitive==GL_LINES?2:a.primitive==GL_POINTS?1:0;
 return width && a.primitive==b.primitive && a.count%width==0 && b.count%width==0 &&
        sameUniforms(a.state,b.state)&&a.model==b.model&&a.projection==b.projection&&
        a.texture==b.texture&&a.blend==b.blend&&a.factors==b.factors;
}
const char* vertexSource=R"(#version 300 es
precision highp float;
layout(location=0) in vec2 pos;
layout(location=1) in vec2 tex;
layout(location=2) in vec4 color;
uniform mat4 transform,model;
out vec2 uv;out vec4 tint;out vec4 eye;
void main(){gl_Position=transform*vec4(pos,0,1);eye=model*vec4(pos,0,1);uv=tex;tint=color;}
)";
const char* fragmentSource=R"(#version 300 es
precision highp float;
in vec2 uv;in vec4 tint;in vec4 eye;
uniform sampler2D image;
uniform bool textured;
uniform float alphaThreshold;
uniform int environment;
uniform vec4 constantColor,planes[2];
uniform ivec2 clipped;
out vec4 result;
void main(){
 if((clipped.x!=0 && dot(eye,planes[0])<0.0)||(clipped.y!=0 && dot(eye,planes[1])<0.0))discard;
 vec4 t=textured?texture(image,uv):vec4(1);
 result=t*tint;
 if(textured && environment==1)result=vec4(min(t.rgb+constantColor.rgb,vec3(1)),t.a);
 if(textured && environment==2)result=vec4(tint.rgb,t.a);
 if(result.a<=alphaThreshold)discard;
}
)";
}
bool initialize(std::string& error){
 queued.clear();commands.clear();
 GLint initialTexture=0,initialActive=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&initialActive);logicalActiveTexture=GLenum(initialActive);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&initialTexture);logicalTexture=GLuint(initialTexture);glActiveTexture(logicalActiveTexture);logicalBlend=glIsEnabled(GL_BLEND);
 const GLenum names[]={GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA};for(size_t i=0;i<4;++i)glGetIntegerv(names[i],&logicalFactors[i]);
 state=State{};model={identity()};projection={identity()};mode=GL_MODELVIEW;saved.clear();uniformsValid=false;
 GLuint shaders[2]{};const char* sources[]={vertexSource,fragmentSource};
 for(int i=0;i<2;++i){shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);glShaderSource(shaders[i],1,&sources[i],nullptr);glCompileShader(shaders[i]);GLint ok=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]{};glGetShaderInfoLog(shaders[i],sizeof(log),nullptr,log);error=log;for(auto s:shaders)if(s)glDeleteShader(s);return false;}}
 program=glCreateProgram();for(auto s:shaders)glAttachShader(program,s);glLinkProgram(program);for(auto s:shaders)glDeleteShader(s);
 GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){char log[2048]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);error=log;shutdown();return false;}
 transformLoc=glGetUniformLocation(program,"transform");modelLoc=glGetUniformLocation(program,"model");textureLoc=glGetUniformLocation(program,"textured");alphaLoc=glGetUniformLocation(program,"alphaThreshold");envLoc=glGetUniformLocation(program,"environment");constantLoc=glGetUniformLocation(program,"constantColor");planesLoc=glGetUniformLocation(program,"planes");clipLoc=glGetUniformLocation(program,"clipped");
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
 for(GLuint i=0;i<3;++i)glEnableVertexAttribArray(i);
 glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
 glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,u)));
 glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,r)));
 useProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
 error.clear();return true;
}
void shutdown(){flush();queued.clear();commands.clear();if(buffer)glDeleteBuffers(1,&buffer);if(vao)glDeleteVertexArrays(1,&vao);if(program)glDeleteProgram(program);buffer=vao=program=0;vertices.clear();triangles.clear();}
void begin(GLenum p){primitive=p;vertices.clear();}
void vertex(float x,float y){vertices.push_back({x,y,u,v,state.color[0],state.color[1],state.color[2],state.color[3]});}
void texCoord(float x,float y){u=x;v=y;}
void color(float r,float g,float b,float a){state.color={r,g,b,a};}
void applyUniforms(const State& state,const Matrix& drawModel,const Matrix& drawProjection){
 if(!uniformsValid || uploadedModel!=drawModel || uploadedProjection!=drawProjection){
  const auto transform=multiply(drawProjection,drawModel);glUniformMatrix4fv(transformLoc,1,GL_FALSE,transform.data());
 }
 if(!uniformsValid || uploadedModel!=drawModel)glUniformMatrix4fv(modelLoc,1,GL_FALSE,drawModel.data());
 if(!uniformsValid || uploadedState.texture!=state.texture)glUniform1i(textureLoc,state.texture);
 if(!uniformsValid || uploadedState.alpha!=state.alpha || uploadedState.threshold!=state.threshold)glUniform1f(alphaLoc,state.alpha?state.threshold:-1.f);
 if(!uniformsValid || uploadedState.env!=state.env || uploadedState.combine!=state.combine)glUniform1i(envLoc,state.env==GL_COMBINE?(state.combine==GL_ADD?1:2):0);
 if(!uniformsValid || uploadedState.constant!=state.constant)glUniform4fv(constantLoc,1,state.constant.data());
 if(!uniformsValid || uploadedState.planes!=state.planes)glUniform4fv(planesLoc,2,state.planes[0].data());
 if(!uniformsValid || uploadedState.clip!=state.clip)glUniform2i(clipLoc,state.clip[0],state.clip[1]);
 uploadedState=state;uploadedModel=drawModel;uploadedProjection=drawProjection;uniformsValid=true;
}

// Resource mutations, readbacks and uncaptured driver state are flush barriers.
void flush(){
 if(commands.empty())return;
 glActiveTexture(GL_TEXTURE0);glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
 glBufferData(GL_ARRAY_BUFFER,queued.size()*sizeof(Vertex),queued.data(),GL_STREAM_DRAW);
 for(const auto& c:commands){
  glBindTexture(GL_TEXTURE_2D,c.texture);
  if(c.blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
  if(c.factors[0]==c.factors[2]&&c.factors[1]==c.factors[3])glBlendFunc(c.factors[0],c.factors[1]);else glBlendFuncSeparate(c.factors[0],c.factors[1],c.factors[2],c.factors[3]);
  applyUniforms(c.state,c.model,c.projection);
  glDrawArrays(c.primitive,GLint(c.first),GLsizei(c.count));
 }
 glBindTexture(GL_TEXTURE_2D,logicalTexture);
 if(logicalBlend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
 if(logicalFactors[0]==logicalFactors[2]&&logicalFactors[1]==logicalFactors[3])glBlendFunc(logicalFactors[0],logicalFactors[1]);else glBlendFuncSeparate(logicalFactors[0],logicalFactors[1],logicalFactors[2],logicalFactors[3]);
 glActiveTexture(logicalActiveTexture);
 queued.clear();commands.clear();
}
// Keep shader/matrix arithmetic identical to the immediate renderer.
void end(){
 if(vertices.empty())return;
 const auto* data=&vertices;
 if(primitive==GL_QUADS){assert(vertices.size()%4==0);triangles.clear();triangles.reserve(vertices.size()/4*6);for(size_t i=0;i+3<vertices.size();i+=4)for(int j:{0,1,2,0,2,3})triangles.push_back(vertices[i+j]);data=&triangles;}
 if(data->size()>kMaxQueuedVertices){
  flush();glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,data->size()*sizeof(Vertex),data->data(),GL_STREAM_DRAW);
  applyUniforms(state,model.back(),projection.back());glDrawArrays(primitive==GL_QUADS?GL_TRIANGLES:primitive,0,GLsizei(data->size()));return;
 }
 if(queued.size()+data->size()>kMaxQueuedVertices||commands.size()>=kMaxQueuedCommands)flush();
 Command command{state,model.back(),projection.back(),primitive==GL_QUADS?GL_TRIANGLES:primitive,logicalTexture,logicalBlend,logicalFactors,queued.size(),data->size()};
 queued.insert(queued.end(),data->begin(),data->end());
 if(!commands.empty()&&compatible(commands.back(),command))commands.back().count+=command.count;
 else commands.push_back(command);
}
void enable(GLenum p){if(p==GL_TEXTURE_2D)state.texture=true;else if(p==GL_ALPHA_TEST)state.alpha=true;else if(p==GL_CLIP_PLANE0||p==GL_CLIP_PLANE1)state.clip[p-GL_CLIP_PLANE0]=true;else driverEnable(p);}
void disable(GLenum p){if(p==GL_TEXTURE_2D)state.texture=false;else if(p==GL_ALPHA_TEST)state.alpha=false;else if(p==GL_CLIP_PLANE0||p==GL_CLIP_PLANE1)state.clip[p-GL_CLIP_PLANE0]=false;else driverDisable(p);}
void matrixMode(GLenum p){assert(p==GL_MODELVIEW||p==GL_PROJECTION);mode=p;}
void loadIdentity(){stack().back()=identity();}
void pushMatrix(){stack().push_back(stack().back());}
void popMatrix(){assert(stack().size()>1);stack().pop_back();}
void ortho(double l,double r,double b,double t,double n,double f){Matrix m{};m[0]=2/(r-l);m[5]=2/(t-b);m[10]=-2/(f-n);m[12]=-(r+l)/(r-l);m[13]=-(t+b)/(t-b);m[14]=-(f+n)/(f-n);m[15]=1;stack().back()=multiply(stack().back(),m);}
void translate(float x,float y,float z){auto m=identity();m[12]=x;m[13]=y;m[14]=z;stack().back()=multiply(stack().back(),m);}
void scale(float x,float y,float z){auto m=identity();m[0]=x;m[5]=y;m[10]=z;stack().back()=multiply(stack().back(),m);}
void rotate(float angle,float x,float y,float z){assert(x==0&&y==0&&z==1);(void)x;(void)y;(void)z;auto m=identity();float a=angle*3.14159265358979323846/180;float c=std::cos(a),s=std::sin(a);m[0]=c;m[1]=s;m[4]=-s;m[5]=c;stack().back()=multiply(stack().back(),m);}
void alphaFunc(GLenum func,float value){assert(func==GL_GREATER);(void)func;state.threshold=value;}
void clipPlane(GLenum plane,const double* p){
 // All callers use affine 2D model transforms. Inverse transpose captures the
 // plane in eye space at specification time, as compatibility OpenGL does.
 const auto&m=model.back();float det=m[0]*m[5]-m[4]*m[1];assert(det!=0);
 float a=(m[5]*p[0]-m[1]*p[1])/det,b=(-m[4]*p[0]+m[0]*p[1])/det;
 state.planes.at(plane-GL_CLIP_PLANE0)={a,b,float(p[2]),float(p[3]-a*m[12]-b*m[13])};
}
void texEnvi(GLenum,GLenum key,GLint value){if(key==GL_TEXTURE_ENV_MODE)state.env=value;else if(key==GL_COMBINE_RGB)state.combine=value;}
void texEnvfv(GLenum,GLenum key,const float*p){assert(key==GL_TEXTURE_ENV_COLOR);(void)key;for(int i=0;i<4;++i)state.constant[i]=p[i];}
void pushAttrib(GLbitfield){Saved s{state,glIsEnabled(GL_BLEND),glIsEnabled(GL_DEPTH_TEST),glIsEnabled(GL_CULL_FACE),0,0};glGetIntegerv(GL_BLEND_SRC_RGB,&s.src);glGetIntegerv(GL_BLEND_DST_RGB,&s.dst);saved.push_back(s);}
void popAttrib(){assert(!saved.empty());auto s=saved.back();saved.pop_back();state=s.state;for(auto pair:{std::pair{GL_BLEND,s.blend},std::pair{GL_DEPTH_TEST,s.depth},std::pair{GL_CULL_FACE,s.cull}}){if(pair.second)enable(pair.first);else disable(pair.first);}blendFunc(s.src,s.dst);}
void readTexture(int width,int height,void* pixels){
 // GLES has no glGetTexImage. A temporary read framebuffer avoids retaining
 // duplicate CPU copies of every sprite just for optional filtering.
 GLint texture=0,oldRead=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
 GLuint fbo;glGenFramebuffers(1,&fbo);bindFramebuffer(GL_READ_FRAMEBUFFER,fbo);framebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
 readPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);bindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);deleteFramebuffers(1,&fbo);
}
void texImage2D(GLenum target,GLint level,GLint internalformat,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const void* pixels){flush();glTexImage2D(target,level,internalformat,width,height,border,format,type,pixels);}
void texSubImage2D(GLenum target,GLint level,GLint xoffset,GLint yoffset,GLsizei width,GLsizei height,GLenum format,GLenum type,const void* pixels){flush();glTexSubImage2D(target,level,xoffset,yoffset,width,height,format,type,pixels);}
void texParameteri(GLenum target,GLenum pname,GLint param){flush();glTexParameteri(target,pname,param);}
void generateMipmap(GLenum target){flush();glGenerateMipmap(target);}
void deleteTextures(GLsizei n,const GLuint* textures){flush();glDeleteTextures(n,textures);for(GLsizei i=0;i<n;++i)if(textures[i]==logicalTexture)logicalTexture=0;}
void deleteFramebuffers(GLsizei n,const GLuint* framebuffers){flush();glDeleteFramebuffers(n,framebuffers);}
void copyTexSubImage2D(GLenum target,GLint level,GLint xoffset,GLint yoffset,GLint x,GLint y,GLsizei width,GLsizei height){flush();glCopyTexSubImage2D(target,level,xoffset,yoffset,x,y,width,height);}
void bindFramebuffer(GLenum target,GLuint framebuffer){flush();glBindFramebuffer(target,framebuffer);}
void framebufferTexture2D(GLenum target,GLenum attachment,GLenum textarget,GLuint texture,GLint level){flush();glFramebufferTexture2D(target,attachment,textarget,texture,level);}
void readPixels(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,void* pixels){flush();glReadPixels(x,y,width,height,format,type,pixels);}
void viewport(GLint x,GLint y,GLsizei width,GLsizei height){flush();glViewport(x,y,width,height);}
void scissor(GLint x,GLint y,GLsizei width,GLsizei height){flush();glScissor(x,y,width,height);}
void clear(GLbitfield mask){flush();glClear(mask);}
void useProgram(GLuint program){flush();glUseProgram(program);}
void blitFramebuffer(GLint srcX0,GLint srcY0,GLint srcX1,GLint srcY1,GLint dstX0,GLint dstY0,GLint dstX1,GLint dstY1,GLbitfield mask,GLenum filter){flush();glBlitFramebuffer(srcX0,srcY0,srcX1,srcY1,dstX0,dstY0,dstX1,dstY1,mask,filter);}
void blendEquation(GLenum mode){flush();glBlendEquation(mode);}
void blendEquationSeparate(GLenum modeRGB,GLenum modeAlpha){flush();glBlendEquationSeparate(modeRGB,modeAlpha);}
void colorMask(GLboolean red,GLboolean green,GLboolean blue,GLboolean alpha){flush();glColorMask(red,green,blue,alpha);}
void depthMask(GLboolean flag){flush();glDepthMask(flag);}
void depthFunc(GLenum func){flush();glDepthFunc(func);}
void cullFace(GLenum mode){flush();glCullFace(mode);}
void frontFace(GLenum mode){flush();glFrontFace(mode);}
void activeTexture(GLenum texture){flush();glActiveTexture(texture);logicalActiveTexture=texture;}
void bindTexture(GLenum target,GLuint texture){if(target!=GL_TEXTURE_2D)flush();glBindTexture(target,texture);if(target==GL_TEXTURE_2D&&logicalActiveTexture==GL_TEXTURE0)logicalTexture=texture;}
void blendFunc(GLenum src,GLenum dst){glBlendFunc(src,dst);logicalFactors={GLint(src),GLint(dst),GLint(src),GLint(dst)};}
void blendFuncSeparate(GLenum s,GLenum d,GLenum sa,GLenum da){glBlendFuncSeparate(s,d,sa,da);logicalFactors={GLint(s),GLint(d),GLint(sa),GLint(da)};}
namespace {
void driverEnable(GLenum cap){if(cap==GL_BLEND)logicalBlend=true;else flush();glEnable(cap);}
void driverDisable(GLenum cap){if(cap==GL_BLEND)logicalBlend=false;else flush();glDisable(cap);}

} // namespace

} // namespace vyruz::gles
