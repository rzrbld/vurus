#include "render/gl_api.h"
#undef glEnable
#undef glDisable
#include <array>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstddef>
namespace vyruz::gles {
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
 glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
 error.clear();return true;
}
void shutdown(){if(buffer)glDeleteBuffers(1,&buffer);if(vao)glDeleteVertexArrays(1,&vao);if(program)glDeleteProgram(program);buffer=vao=program=0;vertices.clear();triangles.clear();}
void begin(GLenum p){primitive=p;vertices.clear();}
void vertex(float x,float y){vertices.push_back({x,y,u,v,state.color[0],state.color[1],state.color[2],state.color[3]});}
void texCoord(float x,float y){u=x;v=y;}
void color(float r,float g,float b,float a){state.color={r,g,b,a};}
void end(){
 if(vertices.empty())return;
 const auto* data=&vertices;
 if(primitive==GL_QUADS){assert(vertices.size()%4==0);triangles.clear();triangles.reserve(vertices.size()/4*6);for(size_t i=0;i+3<vertices.size();i+=4)for(int j:{0,1,2,0,2,3})triangles.push_back(vertices[i+j]);data=&triangles;}
 glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,data->size()*sizeof(Vertex),data->data(),GL_STREAM_DRAW);
 if(!uniformsValid || uploadedModel!=model.back() || uploadedProjection!=projection.back()){
  const auto transform=multiply(projection.back(),model.back());glUniformMatrix4fv(transformLoc,1,GL_FALSE,transform.data());
 }
 if(!uniformsValid || uploadedModel!=model.back())glUniformMatrix4fv(modelLoc,1,GL_FALSE,model.back().data());
 if(!uniformsValid || uploadedState.texture!=state.texture)glUniform1i(textureLoc,state.texture);
 if(!uniformsValid || uploadedState.alpha!=state.alpha || uploadedState.threshold!=state.threshold)glUniform1f(alphaLoc,state.alpha?state.threshold:-1.f);
 if(!uniformsValid || uploadedState.env!=state.env || uploadedState.combine!=state.combine)glUniform1i(envLoc,state.env==GL_COMBINE?(state.combine==GL_ADD?1:2):0);
 if(!uniformsValid || uploadedState.constant!=state.constant)glUniform4fv(constantLoc,1,state.constant.data());
 if(!uniformsValid || uploadedState.planes!=state.planes)glUniform4fv(planesLoc,2,state.planes[0].data());
 if(!uniformsValid || uploadedState.clip!=state.clip)glUniform2i(clipLoc,state.clip[0],state.clip[1]);
 uploadedState=state;uploadedModel=model.back();uploadedProjection=projection.back();uniformsValid=true;
 glDrawArrays(primitive==GL_QUADS?GL_TRIANGLES:primitive,0,GLsizei(data->size()));
}
void enable(GLenum p){if(p==GL_TEXTURE_2D)state.texture=true;else if(p==GL_ALPHA_TEST)state.alpha=true;else if(p==GL_CLIP_PLANE0||p==GL_CLIP_PLANE1)state.clip[p-GL_CLIP_PLANE0]=true;else glEnable(p);}
void disable(GLenum p){if(p==GL_TEXTURE_2D)state.texture=false;else if(p==GL_ALPHA_TEST)state.alpha=false;else if(p==GL_CLIP_PLANE0||p==GL_CLIP_PLANE1)state.clip[p-GL_CLIP_PLANE0]=false;else glDisable(p);}
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
void popAttrib(){assert(!saved.empty());auto s=saved.back();saved.pop_back();state=s.state;for(auto pair:{std::pair{GL_BLEND,s.blend},std::pair{GL_DEPTH_TEST,s.depth},std::pair{GL_CULL_FACE,s.cull}}){if(pair.second)glEnable(pair.first);else glDisable(pair.first);}glBlendFunc(s.src,s.dst);}
void readTexture(int width,int height,void* pixels){
 // GLES has no glGetTexImage. A temporary read framebuffer avoids retaining
 // duplicate CPU copies of every sprite just for optional filtering.
 GLint texture=0,oldRead=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
 GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
 glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);glDeleteFramebuffers(1,&fbo);
}
}
