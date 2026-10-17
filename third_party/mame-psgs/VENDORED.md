# MAME-PSGS

Source: https://github.com/TheCodeTherapy/MAME-PSGS
Commit: `9397be16086d694cffe33b4a4c55d5017aafd257`.
Copyright2019 Marco Gomez / TheCodeTherapy; Apache-2.0, see LICENSE and NOTICE.

`upstream/` retains unmodified shaders from the user-provided local clone.
SHA256 vertex: `fad1952e48e000e216d7f18bc34d027800a54b385fd725e0d3c7114d82ac2b12`.
SHA256 fragment: `e375d66a2b9daf575ead673dca84d10383bc44f0548345c0bd9410963ee0bdca`.

Adapted sources are `src/render/shaders/mame_psgs.vert` and `.frag`.
Changes: explicit GLSL120, viewport aspect uniform, safe root/acos/sine limits,
removal of disabled palette tables, and halation phase preservation before
scanline oversampling changes that variable. Clamp halation RGB before pow to
avoid negative-base NaNs. Original curvature/gamma/scanline/mask/color defaults
are retained. CPU curvature mirror supports mouse targeting.

CMake embeds adapted sources and full license into the engine. The package also
ships LICENSE and NOTICE; no runtime shader files or network fetches are needed.

Brightness/performance follow-up: the live fragment shader now uses one hardware
filtered sample, unit-mean scanline/phosphor masks and a normalized corner fade.
The previous multi-tap reconstruction, halation and filmic grading are removed.
This intentionally changes their appearance/cost following the user's report.
The original curvature and CPU pointer mapping remain unchanged.

Current tuning: corner shade35% (65% transparent) and mild shader-only saturation
0.85 via luminance mix; no additional texture sampling or nonlinear grading.
