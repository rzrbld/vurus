#pragma once
#include <SDL.h>
#include <SDL_opengl.h>
#include <string>
namespace vyruz {
#ifdef __APPLE__
#define VYRUZ_GL_TYPE(type,fn) using type=decltype(&::fn);
VYRUZ_GL_TYPE(PFNGLCREATESHADERPROC,glCreateShader)
VYRUZ_GL_TYPE(PFNGLSHADERSOURCEPROC,glShaderSource)
VYRUZ_GL_TYPE(PFNGLCOMPILESHADERPROC,glCompileShader)
VYRUZ_GL_TYPE(PFNGLGETSHADERIVPROC,glGetShaderiv)
VYRUZ_GL_TYPE(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog)
VYRUZ_GL_TYPE(PFNGLDELETESHADERPROC,glDeleteShader)
VYRUZ_GL_TYPE(PFNGLCREATEPROGRAMPROC,glCreateProgram)
VYRUZ_GL_TYPE(PFNGLATTACHSHADERPROC,glAttachShader)
VYRUZ_GL_TYPE(PFNGLLINKPROGRAMPROC,glLinkProgram)
VYRUZ_GL_TYPE(PFNGLGETPROGRAMIVPROC,glGetProgramiv)
VYRUZ_GL_TYPE(PFNGLGETPROGRAMINFOLOGPROC,glGetProgramInfoLog)
VYRUZ_GL_TYPE(PFNGLDELETEPROGRAMPROC,glDeleteProgram)
VYRUZ_GL_TYPE(PFNGLUSEPROGRAMPROC,glUseProgram)
VYRUZ_GL_TYPE(PFNGLGETUNIFORMLOCATIONPROC,glGetUniformLocation)
VYRUZ_GL_TYPE(PFNGLUNIFORM1IPROC,glUniform1i)
VYRUZ_GL_TYPE(PFNGLUNIFORM2FPROC,glUniform2f)
#undef VYRUZ_GL_TYPE
#endif
// Shared optional GLSL1.20 capture/pass runner for CRT or scene filters.
// Programs/capture storage are independent; functions load only when enabled.
class CrtPass {
public:
    explicit CrtPass(bool scene=false):scene_(scene){}
    bool configureScene(bool fxaa,bool bloom,bool sharpen,std::string& error){
        if(!enable(fxaa||bloom||sharpen,error))return false;
        fxaa_=fxaa;bloom_=bloom;sharpen_=sharpen;return true;
    }
    void setWarm(bool value,bool bright=false){warm_=bright?2:value?1:0;}
    bool enable(bool,std::string& error);
    void shutdown();
    bool enabled()const{return enabled_;}
    bool draw(int width,int height,float logicalWidth,float logicalHeight,std::string& error);
private:
    int warm_=0;GLint warmLocation_=-1;
    bool scene_=false,fxaa_=false,bloom_=false,sharpen_=false;
    GLint outputLocation_=-1,fxaaLocation_=-1,bloomLocation_=-1,sharpenLocation_=-1;
    bool initialize(std::string&);
    GLuint program_=0,texture_=0;int width_=0,height_=0;bool enabled_=false;
    GLint textureLocation_=-1,sizeLocation_=-1,powSizeLocation_=-1,aspectLocation_=-1;
    PFNGLCREATESHADERPROC createShader_=nullptr;PFNGLSHADERSOURCEPROC shaderSource_=nullptr;
    PFNGLCOMPILESHADERPROC compileShader_=nullptr;PFNGLGETSHADERIVPROC getShaderiv_=nullptr;
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog_=nullptr;PFNGLDELETESHADERPROC deleteShader_=nullptr;
    PFNGLCREATEPROGRAMPROC createProgram_=nullptr;PFNGLATTACHSHADERPROC attachShader_=nullptr;
    PFNGLLINKPROGRAMPROC linkProgram_=nullptr;PFNGLGETPROGRAMIVPROC getProgramiv_=nullptr;
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog_=nullptr;PFNGLDELETEPROGRAMPROC deleteProgram_=nullptr;
    PFNGLUSEPROGRAMPROC useProgram_=nullptr;PFNGLGETUNIFORMLOCATIONPROC getUniformLocation_=nullptr;
    PFNGLUNIFORM1IPROC uniform1i_=nullptr;PFNGLUNIFORM2FPROC uniform2f_=nullptr;
};
}
