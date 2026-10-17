# SMAA
Source: https://github.com/iryoku/smaa
Revision: 71c806a838bdd7d517df19192a20f0c61b3ca29d
Imported from the user's local clone. SMAA.hlsl, LICENSE.txt and the Textures/
AreaTex.h and SearchTex.h are unmodified upstream copies.

Vurus uses SMAA 1x Medium, luma edge detection, blending-weight calculation
and neighborhood blending in three GLSL 130 passes. The OpenGL compatibility
wrappers and resource/state management are independently implemented in
src/render/smaa.cpp. Lookup textures use RG8/R8. Display-encoded scene colors
are blended without an sRGB conversion, as allowed by upstream's integration
instructions. No temporal supersampling or MSAA is implied.
