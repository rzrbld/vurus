#pragma once
#include "assets/gaf_sprite.h"
#include "game/content.h"
#include "render/gfx.h"

namespace vyruz {
// Native 404400/404460: four authored pictures, followed by the clipped ruler.
// Progress comes from completed loading work; this renderer has no time clock.
class LoadingScreen {
public:
    bool load(const ContentReader&, std::string& error);
    void begin(bool cycle=true);
    void progress(Gfx&, float fraction);
    void clear(Gfx&);
    size_t pictureNumber() const { return picture_+1; }
private:
    GafAnimation animation_;
    std::vector<GLuint> textures_;
    size_t picture_=0;
    bool begun_=false;
    void draw(Gfx&, size_t sequence, float fraction);
};
}
