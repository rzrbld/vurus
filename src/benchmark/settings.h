#pragma once
#include "game/options.h"

namespace vyruz::benchmark {
// Copy only presentation preferences. Benchmark audio, input, difficulty and
// player/storage identity must never come from the player's menu settings.
inline GameOptions resolveOptions(const GameOptions* menu, bool gles) {
    GameOptions out;
    out.width = 640; out.height = 480; out.vsync = false; out.frameCap = 0;
    out.groundDetails = false; out.music = false; out.fpsCounter = true;
    out.playerName = "Benchmark";
    if (menu) {
        out.width = menu->width; out.height = menu->height; out.windowMode = menu->windowMode;
        out.gamma = menu->gamma; out.spriteFiltering = menu->spriteFiltering;
        out.hudAspect = menu->hudAspect;
        out.shadows = menu->shadows; out.reactorFx = menu->reactorFx;
        out.groundDetails = menu->groundDetails; out.vignette = menu->vignette;
        out.crt = menu->crt; out.crtRetro = menu->crtRetro; out.crtBright = menu->crtBright;
        out.fxaa = menu->fxaa; out.smaa = menu->smaa;
        out.bloom = menu->bloom; out.sharpen = menu->sharpen;
    }
    // Same GLES capability policy as applyOptionsRuntime in the normal game.
    if (gles) out.crt = out.crtRetro = out.crtBright = out.fxaa = out.smaa = out.bloom = out.sharpen = false;
    return out;
}
// Fixed-display SDL backends can create a usable native-sized window while
// refusing the requested window size. Retry only windowed size failures; do
// not hide unsupported fullscreen modes or mutate the saved menu settings.
inline bool useExistingWindowSize(GameOptions& options, int width, int height) {
    if ((options.windowMode != 0 && options.windowMode != 3) || width <= 0 || height <= 0 ||
        (options.width == width && options.height == height)) return false;
    options.width = width; options.height = height;
    return true;
}
} // namespace vyruz::benchmark
