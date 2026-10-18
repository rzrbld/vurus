#pragma once
#include <SDL.h>
#include "render/gl_api.h"
#include <array>
#include <string>
namespace vyruz {
class SmaaPass {
public:
 bool enable(bool,std::string&);
 bool enabled()const{return enabled_;}
 bool draw(int,int,float,float,std::string&);
 void shutdown();
private:
 bool initialize(std::string&);bool allocate(int,int,std::string&);void releaseTargets();
 bool enabled_=false;int width_=0,height_=0;
 std::array<GLuint,3> programs_{},targets_{};std::array<GLint,3> metrics_{};
 GLuint fbo_=0,area_=0,search_=0;
#if !defined(VYRUZ_ENABLE_SMAA) || VYRUZ_ENABLE_SMAA
#define SMAA_GL(type,name) type name##_=nullptr;
#include "render/smaa_gl.inc"
#undef SMAA_GL
#endif
};
}
