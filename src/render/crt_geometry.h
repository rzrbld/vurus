#pragma once
#include <algorithm>
#include <cmath>
namespace vyruz {
// MAME-PSGS curvature mapping, mirrored for mouse hit-testing. Normalized UVs
// use GL bottom-up Y; simulation/camera coordinates are never changed.
struct CrtPoint { float x=0,y=0; };
struct CrtGeometry {
    CrtPoint aspect;float stretchX=0,stretchY=0,stretchScale=1;
    static constexpr float distance=2,radius=1.75f;
    const float sy=std::sin(-.01f),cy=std::cos(-.01f);
    explicit CrtGeometry(float width,float height):aspect{width/std::max(width,height),height/std::max(width,height)}{
        const auto c=backward({0,-radius*sy/(1+radius/distance*cy)});
        const float ax=.5f*aspect.x,ay=.5f*aspect.y;
        const CrtPoint lo{forward({-ax,c.y}).x/aspect.x,forward({c.x,-ay}).y/aspect.y};
        const CrtPoint hi{forward({ax,c.y}).x/aspect.x,forward({c.x,ay}).y/aspect.y};
        stretchX=(hi.x+lo.x)*aspect.x*.5f;stretchY=(hi.y+lo.y)*aspect.y*.5f;stretchScale=std::max(hi.x-lo.x,hi.y-lo.y);
    }
    CrtPoint backward(CrtPoint xy) const {
        const float len2=xy.x*xy.x+xy.y*xy.y;if(len2<.000001f)return xy;
        const float a0=len2+distance*distance,b0=2*(radius*(xy.y*sy-distance*cy)-distance*distance),c0=distance*distance+2*radius*distance*cy;
        const float c=(-b0-std::sqrt(std::max(0.f,b0*b0-4*a0*c0)))/(2*a0);
        const CrtPoint p{c*xy.x/radius,(c*xy.y+radius*sy)/radius};
        const float tang=sy/cy,pocY=p.y/cy,a=tang*tang+1,b=-2*pocY*tang,cc=p.x*p.x+pocY*pocY-1,disc=b*b-4*a*cc;
        if(disc<0)return xy;
        const float root=(-b+std::sqrt(disc))/(2*a),r=radius*std::acos(std::clamp(root,-1.f,1.f)),sr=std::sin(r/radius),scale=std::abs(sr)>1e-6f?r/sr:radius;
        return {p.x*scale,(p.y-root*sy)/cy*scale};
    }
    CrtPoint forward(CrtPoint uv) const {
        const float r=std::max(1e-5f,std::sqrt(uv.x*uv.x+uv.y*uv.y)),s=std::sin(r/radius)/r;
        uv.x*=s;uv.y*=s;const float x=1-std::cos(r/radius),d=distance/radius+x*cy+uv.y*sy;
        return {distance*uv.x/d,distance*(uv.y*cy-x*sy)/d};
    }
    CrtPoint source(float u,float v) const {
        const auto p=backward({(u-.5f)*aspect.x*stretchScale+stretchX,(v-.5f)*aspect.y*stretchScale+stretchY});
        return {p.x/aspect.x+.5f,p.y/aspect.y+.5f};
    }
};
}
