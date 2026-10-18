#pragma once
// Desktop keeps the established compatibility renderer. GLES implements only
// the 2D operations used by this engine, without an external GL translation layer.
#if defined(VYRUZ_GLES) && VYRUZ_GLES
#include <GLES3/gl3.h>
#include "render/gles_compat.h"
#else
#include <SDL_opengl.h>
#endif
