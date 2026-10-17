#include "render/smaa.h"
#if !defined(VYRUZ_ENABLE_SMAA) || VYRUZ_ENABLE_SMAA
#include "smaa_shader.h"
#include "../../third_party/smaa/AreaTex.h"
#include "../../third_party/smaa/SearchTex.h"
#include <cstdio>
namespace vyruz {
namespace {
void textureParameters(){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
std::string shaderText(int pass,bool vertex){
 std::string text="#version 130\n#define SMAA_GLSL_3\n#define SMAA_PRESET_MEDIUM\nuniform vec4 metrics;\n#define SMAA_RT_METRICS metrics\n";
 text+=vertex?"#define SMAA_INCLUDE_VS 1\n#define SMAA_INCLUDE_PS 0\n":"#define SMAA_INCLUDE_VS 0\n#define SMAA_INCLUDE_PS 1\n";
 for(const char* chunk:smaaSourceChunks)text+=chunk;
 text+=vertex?"\nout vec2 uv;out vec2 pixel;out vec4 offsets[3];\n":"\nin vec2 uv;in vec2 pixel;in vec4 offsets[3];out vec4 fragColor;uniform sampler2D colorTex,edgesTex,areaTex,searchTex,blendTex;\n";
 if(vertex){text+="void main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;uv=gl_MultiTexCoord0.xy;pixel=vec2(0);offsets[0]=offsets[1]=offsets[2]=vec4(0);";
  text+=pass==0?"SMAAEdgeDetectionVS(uv,offsets);}":pass==1?"SMAABlendingWeightCalculationVS(uv,pixel,offsets);}":"SMAANeighborhoodBlendingVS(uv,offsets[0]);}";
 }else text+=pass==0?"void main(){fragColor=vec4(SMAALumaEdgeDetectionPS(uv,offsets,colorTex),0,0);}":pass==1?"void main(){fragColor=SMAABlendingWeightCalculationPS(uv,pixel,offsets,edgesTex,areaTex,searchTex,vec4(0));}":"void main(){fragColor=SMAANeighborhoodBlendingPS(uv,offsets[0],colorTex,blendTex);}";
 return text;
}
}
bool SmaaPass::initialize(std::string& error){
 int major=0;const auto* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));if(!version||std::sscanf(version,"%d",&major)!=1||major<3){error="SMAA requires OpenGL 3.0 or newer";return false;}
#define SMAA_GL(type,name) name##_=reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name));if(!name##_){error="SMAA is unavailable: missing gl" #name;return false;}
#include "render/smaa_gl.inc"
#undef SMAA_GL
 GLint previous=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
 for(int pass=0;pass<3;++pass){std::array<GLuint,2> shaders{};bool valid=true;
  for(int stage=0;stage<2;++stage){const auto source=shaderText(pass,stage==0);const char* ptr=source.c_str();shaders[stage]=CreateShader_(stage?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);ShaderSource_(shaders[stage],1,&ptr,nullptr);CompileShader_(shaders[stage]);GLint ok=0;GetShaderiv_(shaders[stage],GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]={};GetShaderInfoLog_(shaders[stage],sizeof(log),nullptr,log);error="SMAA compile: "+std::string(log);valid=false;break;}}
  if(valid){programs_[pass]=CreateProgram_();for(auto shader:shaders)AttachShader_(programs_[pass],shader);LinkProgram_(programs_[pass]);GLint ok=0;GetProgramiv_(programs_[pass],GL_LINK_STATUS,&ok);if(!ok){char log[2048]={};GetProgramInfoLog_(programs_[pass],sizeof(log),nullptr,log);error="SMAA link: "+std::string(log);valid=false;}}
  for(auto shader:shaders)if(shader)DeleteShader_(shader);
  if(!valid){shutdown();UseProgram_(previous);return false;}
  metrics_[pass]=GetUniformLocation_(programs_[pass],"metrics");UseProgram_(programs_[pass]);
  for(const auto& binding:{std::pair{"colorTex",0},std::pair{"edgesTex",0},std::pair{"areaTex",1},std::pair{"searchTex",2},std::pair{"blendTex",1}})Uniform1i_(GetUniformLocation_(programs_[pass],binding.first),binding.second);
 }
 UseProgram_(previous);return true;
}
bool SmaaPass::enable(bool value,std::string& error){if(value&&!programs_[0]&&!initialize(error))return false;enabled_=value;if(!value)releaseTargets();error.clear();return true;}
void SmaaPass::releaseTargets(){glDeleteTextures(3,targets_.data());targets_={};if(area_)glDeleteTextures(1,&area_);if(search_)glDeleteTextures(1,&search_);area_=search_=0;if(fbo_&&DeleteFramebuffers_)DeleteFramebuffers_(1,&fbo_);fbo_=0;width_=height_=0;}
void SmaaPass::shutdown(){releaseTargets();for(auto p:programs_)if(p&&DeleteProgram_)DeleteProgram_(p);programs_={};enabled_=false;}
bool SmaaPass::allocate(int width,int height,std::string& error){
 if(width_==width&&height_==height)return true;
 GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);if(width>maximum||height>maximum){error="SMAA exceeds GPU texture limit";return false;}
 releaseTargets();glGenTextures(3,targets_.data());
 for(auto t:targets_){glBindTexture(GL_TEXTURE_2D,t);textureParameters();glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);}
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);
 glGenTextures(1,&area_);glBindTexture(GL_TEXTURE_2D,area_);textureParameters();glTexImage2D(GL_TEXTURE_2D,0,GL_RG8,AREATEX_WIDTH,AREATEX_HEIGHT,0,GL_RG,GL_UNSIGNED_BYTE,areaTexBytes);
 glGenTextures(1,&search_);glBindTexture(GL_TEXTURE_2D,search_);textureParameters();glTexImage2D(GL_TEXTURE_2D,0,GL_R8,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,0,GL_RED,GL_UNSIGNED_BYTE,searchTexBytes);
 GenFramebuffers_(1,&fbo_);
 if(glGetError()!=GL_NO_ERROR){error="SMAA buffer allocation failed";releaseTargets();return false;}
 width_=width;height_=height;return true;
}
bool SmaaPass::draw(int width,int height,float logicalWidth,float logicalHeight,std::string& error){
 if(!enabled_||width<=0||height<=0)return true;
 GLint oldProgram=0,oldDraw=0,oldRead=0,oldActive=0,oldUnpack=0;
 glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);glGetIntegerv(GL_UNPACK_ALIGNMENT,&oldUnpack);
 glPushAttrib(GL_ALL_ATTRIB_BITS);ActiveTexture_(GL_TEXTURE0);
 bool ok=allocate(width,height,error);
 if(ok){glBindTexture(GL_TEXTURE_2D,targets_[0]);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);if(glGetError()!=GL_NO_ERROR){error="SMAA capture failed";ok=false;}}
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,width,height);
 for(int pass=0;ok&&pass<3;++pass){
  if(pass<2){BindFramebuffer_(GL_FRAMEBUFFER,fbo_);FramebufferTexture2D_(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,targets_[pass+1],0);glDrawBuffer(GL_COLOR_ATTACHMENT0);if(CheckFramebufferStatus_(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){error="SMAA framebuffer incomplete";ok=false;break;}glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);}
  else {BindFramebuffer_(GL_DRAW_FRAMEBUFFER,oldDraw);}
  const GLuint tex0=pass==1?targets_[1]:targets_[0];ActiveTexture_(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex0);
  ActiveTexture_(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,pass==1?area_:targets_[2]);ActiveTexture_(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,search_);ActiveTexture_(GL_TEXTURE0);
  UseProgram_(programs_[pass]);Uniform4f_(metrics_[pass],1.f/width,1.f/height,float(width),float(height));
  // Oversized triangle avoids duplicate shading along a full-screen quad seam.
  glBegin(GL_TRIANGLES);glTexCoord2f(0,1);glVertex2f(0,0);glTexCoord2f(2,1);glVertex2f(logicalWidth*2,0);glTexCoord2f(0,-1);glVertex2f(0,logicalHeight*2);glEnd();
 }
 glPopMatrix();BindFramebuffer_(GL_DRAW_FRAMEBUFFER,oldDraw);BindFramebuffer_(GL_READ_FRAMEBUFFER,oldRead);UseProgram_(oldProgram);glPopAttrib();ActiveTexture_(oldActive);glPixelStorei(GL_UNPACK_ALIGNMENT,oldUnpack);
 if(ok&&glGetError()!=GL_NO_ERROR){error="SMAA draw failed";ok=false;}return ok;
}
}

#else
namespace vyruz {
bool SmaaPass::enable(bool enabled,std::string& error){if(enabled){error="SMAA is unavailable in this platform build; use FXAA instead";return false;}enabled_=false;error.clear();return true;}
bool SmaaPass::draw(int,int,float,float,std::string& error){error.clear();return true;}
void SmaaPass::shutdown(){enabled_=false;}
}
#endif
