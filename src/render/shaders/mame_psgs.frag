#version 120
// Vurus adaptation, 2026-10-17: adaptive aspect, safe roots/center; see third_party/mame-psgs/NOTICE.
/*  CRT shader
 *
 *  Copyright (C) 2019 Marco Gomez
 *  @TheCodeTherapy | https://mgz.me | http://github.com/TheCodeTherapy
 *
 *  This is a GLSL CRT simulation shader that I wrote to use with MAME emulator with a special
 *  method to simulate a variety of old-school computer hardware custom color palettes, through
 *  a special dithering matrix / color approximation method.
 *
 *  It was tested with several games on MAME 0.191, but as per MAME's documentation it should work
 *  fine with any recent version of MAME using OpenGL. I wrote it as GLSL (OpenGL) concerning the
 *  number of systems (including Linux, Raspberry Pi and Odroid XU4 - Android based ones) that
 *  can't run MAME with DirectX 9.0, but it is on my plans to write the HLSL version ASAP.
 *
 */

uniform sampler2D mpass_texture;
uniform int retroWarm;
uniform vec2 color_texture_sz;
uniform vec2 color_texture_pow2_sz;

varying vec2 texCoord;
varying vec2 one;

varying float CRTgamma;
varying float monitorgamma;

varying vec2 overscan;
varying vec2 aspect;

varying float d;
varying float R;

varying float cornersize;
varying float cornersmooth;
varying float filmicReinhard;
varying float palette_emulation;

varying float halation;
varying float saturation;

varying vec3 stretch;
varying vec2 sinangle;
varying vec2 cosangle;

float intersect(vec2 xy) {
    float A = dot(xy, xy) + d * d;
    float B = 2.0 * (R * (dot(xy, sinangle) - d * cosangle.x * cosangle.y) - d * d);
    float C = d * d + 2.0 * R * d * cosangle.x * cosangle.y;
    return (-B - sqrt(max(0.0, B * B - 4.0 * A * C))) / (2.0 * A);
}

vec2 bkwtrans(vec2 xy) {
    if (length(xy) < 0.001) {
        return xy;
    }
    float c = intersect(xy);
    vec2 point = vec2(c) * xy;
    point -= vec2(-R) * sinangle;
    point /= vec2(R);
    vec2 tang = sinangle / cosangle;
    vec2 poc = point / cosangle;
    float A = dot(tang, tang) + 1.0;
    float B = -2.0 * dot(poc, tang);
    float C = dot(poc, poc) - 1.0;
    float discriminant = B * B - 4.0 * A * C;
    if (discriminant < 0.0) {
        return xy;
    }
    float a = (-B + sqrt(discriminant)) / (2.0 * A);
    vec2 uv = (point - a * sinangle) / cosangle;
    float r = R * acos(clamp(a, -1.0, 1.0));
    float sr = sin(r / R);
    return uv * (abs(sr) > 1e-6 ? r / sr : R);
}

vec2 transform(vec2 coord) {
    coord *= color_texture_pow2_sz / color_texture_sz;
    coord = (coord - vec2(0.5)) * aspect * stretch.z + stretch.xy;
    return (bkwtrans(coord) / overscan / aspect + vec2(0.5)) * color_texture_sz / color_texture_pow2_sz;
}

float corner(vec2 coord) {
    coord *= color_texture_pow2_sz / color_texture_sz;
    coord = (coord - vec2(0.5)) * overscan + vec2(0.5);
    coord = min(coord, vec2(1.0) - coord) * aspect;
    vec2 cdist = vec2(cornersize);
    coord = (cdist - min(coord, cdist));
    float dist = sqrt(dot(coord, coord));
    return clamp((cdist.x - dist) / cornersize, 0.0, 1.0);
}

// Bright-only highlight extraction; four filtered neighbors in the existing pass.
vec3 highlight(vec3 c) {
    return c * smoothstep(0.55, 0.9, dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

// Vurus low-cost presentation path: one hardware-filtered sample. Keep source
// brightness; normalized subtle masks replace the expensive nonlinear
// reconstruction, filmic grading and halation. Fade scanlines when undersampled.
void main() {
    vec2 xy = transform(texCoord);
    // Only a faint extra edge shade: the original gameplay vignette is separate.
    float edge = mix(1.0, corner(xy), 0.35);
    vec3 color = texture2D(mpass_texture, xy).rgb;
    if(retroWarm==2) {
        vec2 delta = vec2(1.5) / color_texture_sz;
        vec3 glow = highlight(texture2D(mpass_texture, xy + delta).rgb);
        glow += highlight(texture2D(mpass_texture, xy - delta).rgb);
        glow += highlight(texture2D(mpass_texture, xy + vec2(delta.x,-delta.y)).rgb);
        glow += highlight(texture2D(mpass_texture, xy + vec2(-delta.x,delta.y)).rgb);
        glow = glow * 0.1875 + highlight(color) * 0.25;
        // Screen-style addition retains highlight detail without clipping.
        color += (1.0-color) * glow * 0.25;
        color += 0.03 * color * (1.0-color);
    }
    // Mild CRT color softening. Unit-sum luma keeps grays/brightness intact;
    // a convex mix avoids clipping. No extra sampling, powers or lookup tables.
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, 0.85);
    if(retroWarm!=0) {
        // Mild warm display tint; preserve black and avoid red highlight clipping.
        color.r += 0.04 * color.r * (1.0-color.r);
        color.b *= 0.92;
    }
    float row = xy.y * color_texture_sz.y;
    float lineStrength = 0.08 * (1.0 - smoothstep(0.5, 1.0, fwidth(row)));
    float scanline = 1.0 - lineStrength * cos(6.28318530718 * row);
    vec3 mask = mix(vec3(1.04, 0.96, 1.04), vec3(0.96, 1.04, 0.96),
                    mod(floor(gl_FragCoord.x), 2.0));
    // Both masks have unit mean. Black stays black and midtones are retained.
    color *= scanline * mask * edge;
    if (xy.x < 0.0 || xy.x > 1.0 || xy.y < 0.0 || xy.y > 1.0) color = vec3(0.0);
    gl_FragColor = vec4(color, 1.0);
}
