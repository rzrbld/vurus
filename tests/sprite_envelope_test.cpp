#include "render/sprite_envelope.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace vyruz;
int main()try{
 unsigned long long checks=0,rejected=0;std::mt19937 random(20261022);
 const auto need=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Envelope rejected a visible pose or invalid input");};
 for(unsigned fixture=0;fixture<25000;++fixture){
  GafAnimation a;a.frames.resize(4);GafSequence seq;
  for(unsigned i=0;i<4;++i){auto& f=a.frames[i];f.image.width=1+random()%2048;f.image.height=1+random()%2048;
   f.originX=int(random()%20001)-10000;f.originY=int(random()%20001)-10000;f.flippedOriginX=int(random()%20001)-10000;f.flippedOriginY=int(random()%20001)-10000;
   GafSubframe p;p.frameIndex=i;p.offsetX=int(random()%20001)-10000;p.offsetY=int(random()%20001)-10000;p.flags=(random()%4)<<8;seq.subframes.push_back(p);}
  seq.subframes.push_back({GafSubframe::Blank});const auto e=spriteSequenceEnvelope(a,seq);need(!e.outside(0,0,0,0,640,480,NAN));need(!e.outside(0,0,0,0,640,480,INFINITY));need(e.ready&&e.valid&&e.drawable);
  for(unsigned sample=0;sample<24;++sample){
   float x=int(random()%20001)-10000,y=int(random()%20001)-10000;if(sample>=16){x+=16777216.f;y-=16777216.f;}
   const float altitude=int(random()%40001)-20000+.25f;
   for(const float height:{0.f,altitude}) {
   const auto& p=seq.subframes[sample%4];const auto& f=a.frames[p.frameIndex];const bool fx=p.flags&0x100,fy=p.flags&0x200;
   const float left=x+0.f+(fx?-p.offsetX:p.offsetX)+(fx?f.flippedOriginX:f.originX);
   const float top=y+0.f-height+(fy?-p.offsetY:p.offsetY)+(fy?f.flippedOriginY:f.originY);
   float cx=left,cy=top;if(sample%3==0)cx=left+f.image.width;if(sample%3==1)cy=top+f.image.height;
   cx=std::nextafter(cx,sample%2?INFINITY:-INFINITY);cy=std::nextafter(cy,sample%2?INFINITY:-INFINITY);
   const bool outside=e.outside(x,y,cx,cy,640,480,height);rejected+=outside;
   for(const auto& q:seq.subframes){if(q.frameIndex==GafSubframe::Blank)continue;const auto& b=a.frames[q.frameIndex];
    const float l=x+0.f+((q.flags&0x100)?-q.offsetX:q.offsetX)+((q.flags&0x100)?b.flippedOriginX:b.originX);
    const float t=y+0.f-height+((q.flags&0x200)?-q.offsetY:q.offsetY)+((q.flags&0x200)?b.flippedOriginY:b.originY);
    const bool visible=!(l+b.image.width<=cx||l>=cx+640||t+b.image.height<=cy||t>=cy+480||b.image.width<=0||b.image.height<=0);
    need(!outside||!visible);
   }
   }
  }
 }
 GafAnimation a;a.frames.resize(1);a.frames[0].image.width=a.frames[0].image.height=8;GafSequence seq;seq.subframes.push_back({0});
 auto e=spriteSequenceEnvelope(a,seq);need(!e.outside(NAN,0,0,0,640,480));need(!e.outside(INFINITY,0,0,0,640,480));need(!e.outside(0,0,NAN,0,640,480));need(!e.outside(0,0,0,0,0,480));
 seq.subframes[0].frameIndex=2;e=spriteSequenceEnvelope(a,seq);need(!e.valid&&!e.outside(10000,0,0,0,640,480));
 seq.subframes[0]={0,std::numeric_limits<int>::min(),0,0,0x100};e=spriteSequenceEnvelope(a,seq);need(!e.valid);
 seq.subframes={{GafSubframe::Blank}};e=spriteSequenceEnvelope(a,seq);need(e.valid&&!e.drawable&&e.outside(0,0,0,0,640,480));
 seq.subframes={{0}};e=spriteSequenceEnvelope(a,seq);need(e.drawable);a.frames[0].originX=-1000;e=spriteSequenceEnvelope(a,seq);need(e.outside(0,0,0,0,640,480));
 std::cout<<"PASS "<<checks<<" conservative envelope checks; "<<rejected<<" early rejections; invalid/blank/rebuild controls\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
