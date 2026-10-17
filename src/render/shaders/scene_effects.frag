#version 120
// Independent lightweight scene filters; UI is drawn afterwards.
uniform sampler2D mpass_texture;
uniform vec2 outputSize;
uniform int useFxaa, useBloom, useSharpen;
varying vec2 texCoord;
float luma(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
vec3 sampleAt(vec2 p) { return texture2D(mpass_texture,p).rgb; }
vec3 brightColor(vec3 c) { return c*smoothstep(0.65,0.95,luma(c)); }
vec3 bright(vec2 p) { return brightColor(sampleAt(p)); }
void main() {
 vec2 p=texCoord, px=1.0/outputSize;
 vec3 center=sampleAt(p), result=center;
 vec3 nw=vec3(0),ne=vec3(0),sw=vec3(0),se=vec3(0);
 if(useFxaa!=0 || useSharpen!=0) {
  nw=sampleAt(p+px*vec2(-1,1)); ne=sampleAt(p+px*vec2(1,1));
  sw=sampleAt(p+px*vec2(-1,-1)); se=sampleAt(p+px*vec2(1,-1));
 }
 if(useFxaa!=0) {
  float m=luma(center), a=luma(nw), b=luma(ne), c=luma(sw), d=luma(se);
  float low=min(m,min(min(a,b),min(c,d))), high=max(m,max(max(a,b),max(c,d)));
  if(high-low>max(0.0312,high*0.125)) {
   vec2 direction=vec2(-((a+b)-(c+d)),(a+c)-(b+d));
   float reduce=max((a+b+c+d)*0.03125,0.0078125);
   direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-8),vec2(8))*px;
   vec3 inner=0.5*(sampleAt(p-direction/6.0)+sampleAt(p+direction/6.0));
   vec3 outer=inner*0.5+0.25*(sampleAt(p-direction*0.5)+sampleAt(p+direction*0.5));
   float value=luma(outer);result=(value<low||value>high)?inner:outer;
  }
 }
 if(useSharpen!=0) {
  // Reuse FXAA's four diagonal taps. Their second-order detail is twice the
  // axial kernel's, so halve its gain to preserve gentle sharpening strength.
  vec3 detail=center-(nw+ne+sw+se)*0.25;
  result+=clamp(detail*0.09,vec3(-0.025),vec3(0.025));
 }
 if(useBloom!=0) {
  // Compact filtered halo: four bilinear taps plus the cached center.
  // Avoid eight separate wide-neighborhood reads at full display resolution.
  vec2 r=px*1.5;
  vec3 glow=brightColor(center)*0.25;
  glow+=(bright(p+r)+bright(p-r)+bright(p+vec2(r.x,-r.y))+bright(p+vec2(-r.x,r.y)))*0.1875;
  result+=glow*0.12*(vec3(1)-result);
 }
 gl_FragColor=vec4(clamp(result,0.0,1.0),1);
}
