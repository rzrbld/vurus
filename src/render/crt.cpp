#if !VYRUZ_GLES
#include "render/crt.h"
#include "crt_shaders.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
namespace vyruz {
bool CrtPass::initialize(std::string& error){
    int major=0,minor=0;const auto* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if(!version||std::sscanf(version,"%d.%d",&major,&minor)!=2||major<2||(major==2&&minor<1)){error="Graphics effects require OpenGL2.1 or newer";return false;}
#define LOAD(member,name) member=reinterpret_cast<decltype(member)>(SDL_GL_GetProcAddress(name));if(!member){error="Graphics effects require OpenGL2.1 shader support";return false;}
    LOAD(createShader_,"glCreateShader") LOAD(shaderSource_,"glShaderSource") LOAD(compileShader_,"glCompileShader") LOAD(getShaderiv_,"glGetShaderiv") LOAD(getShaderInfoLog_,"glGetShaderInfoLog") LOAD(deleteShader_,"glDeleteShader")
    LOAD(createProgram_,"glCreateProgram") LOAD(attachShader_,"glAttachShader") LOAD(linkProgram_,"glLinkProgram") LOAD(getProgramiv_,"glGetProgramiv") LOAD(getProgramInfoLog_,"glGetProgramInfoLog") LOAD(deleteProgram_,"glDeleteProgram") LOAD(useProgram_,"glUseProgram") LOAD(getUniformLocation_,"glGetUniformLocation") LOAD(uniform1i_,"glUniform1i") LOAD(uniform2f_,"glUniform2f")
#undef LOAD
    std::array<GLuint,2> stages{};const std::array<const char*,2> sources={scene_?sceneVertex:crtVertex,scene_?sceneFragment:crtFragment};
    for(int i=0;i<2;++i){stages[i]=createShader_(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);if(stages[i]){shaderSource_(stages[i],1,&sources[i],nullptr);compileShader_(stages[i]);}GLint ok=0;if(stages[i])getShaderiv_(stages[i],GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]={};if(stages[i])getShaderInfoLog_(stages[i],sizeof(log),nullptr,log);error=std::string("Graphics shader compilation failed: ")+log;for(auto shader:stages)if(shader)deleteShader_(shader);return false;}}
    const auto program=createProgram_();if(!program){for(auto shader:stages)deleteShader_(shader);error="Cannot create graphics program";return false;}
    for(auto shader:stages){attachShader_(program,shader);}
    linkProgram_(program);
    for(auto shader:stages){deleteShader_(shader);}
    GLint ok=0;getProgramiv_(program,GL_LINK_STATUS,&ok);if(!ok){char log[2048]={};getProgramInfoLog_(program,sizeof(log),nullptr,log);deleteProgram_(program);error=std::string("Graphics shader link failed: ")+log;return false;}
    program_=program;warmLocation_=getUniformLocation_(program_,"retroWarm");textureLocation_=getUniformLocation_(program_,"mpass_texture");sizeLocation_=getUniformLocation_(program_,"color_texture_sz");powSizeLocation_=getUniformLocation_(program_,"color_texture_pow2_sz");aspectLocation_=getUniformLocation_(program_,"crtAspect");
    outputLocation_=getUniformLocation_(program_,"outputSize");fxaaLocation_=getUniformLocation_(program_,"useFxaa");bloomLocation_=getUniformLocation_(program_,"useBloom");sharpenLocation_=getUniformLocation_(program_,"useSharpen");return true;
}
bool CrtPass::enable(bool enabled,std::string& error){
    if(enabled&&!program_&&!initialize(error))return false;
    enabled_=enabled;if(!enabled&&texture_){glDeleteTextures(1,&texture_);texture_=0;width_=height_=0;}error.clear();return true;
}
void CrtPass::shutdown(){if(texture_)glDeleteTextures(1,&texture_);texture_=0;width_=height_=0;if(program_&&deleteProgram_)deleteProgram_(program_);program_=0;enabled_=false;}
bool CrtPass::draw(int width,int height,float logicalWidth,float logicalHeight,std::string& error){
    if(!enabled_||width<=0||height<=0)return true;
    GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);if(width>maxSize||height>maxSize){error="Graphics framebuffer exceeds GPU texture limit";return false;}
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT|GL_TRANSFORM_BIT);GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    if(!texture_){glGenTextures(1,&texture_);}
    glBindTexture(GL_TEXTURE_2D,texture_);
    if(width_!=width||height_!=height){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
        if(glGetError()!=GL_NO_ERROR){glPopAttrib();error="Graphics framebuffer allocation failed";return false;}
        width_=width;height_=height;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
    if(glGetError()!=GL_NO_ERROR){glPopAttrib();error="Graphics framebuffer copy failed";return false;}
    useProgram_(program_);uniform1i_(warmLocation_,warm_);uniform1i_(textureLocation_,0);uniform2f_(sizeLocation_,logicalWidth,logicalHeight);uniform2f_(powSizeLocation_,logicalWidth,logicalHeight);
    uniform2f_(outputLocation_,float(width),float(height));uniform1i_(fxaaLocation_,fxaa_);uniform1i_(bloomLocation_,bloom_);uniform1i_(sharpenLocation_,sharpen_);
    const float longest=std::max(logicalWidth,logicalHeight);uniform2f_(aspectLocation_,logicalWidth/longest,logicalHeight/longest);
    glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_SCISSOR_TEST);glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glBegin(GL_QUADS);glTexCoord2f(0,1);glVertex2f(0,0);glTexCoord2f(1,1);glVertex2f(logicalWidth,0);glTexCoord2f(1,0);glVertex2f(logicalWidth,logicalHeight);glTexCoord2f(0,0);glVertex2f(0,logicalHeight);glEnd();
    glPopMatrix();useProgram_(oldProgram);glPopAttrib();return true;
}
}

#else
#include "render/crt.h"
namespace vyruz {
bool CrtPass::enable(bool value,std::string& error){if(value){error="Post-processing is unavailable in this GLES build";return false;}enabled_=false;error.clear();return true;}
void CrtPass::shutdown(){enabled_=false;}
bool CrtPass::draw(int,int,float,float,std::string& error){error.clear();return true;}
}
#endif
