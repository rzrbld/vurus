#pragma once
#include "render/hud.h"

namespace vyruz {
// Logical 640x480 DOS-style panels, using the game's small gold menu font.
struct CommanderUi {
    Gfx& gfx;
    HudRenderer& hud;
    float text(std::string_view value,float x,float y,float width,float scale=1.15f,bool wrap=false) const {
        glPushMatrix();glTranslatef(x,y,0);glScalef(scale,scale,1);
        const float height=hud.drawMenuText(gfx,value,0,0,4,width/scale,wrap);
        glPopMatrix();return height*scale;
    }
    static void box(float l,float t,float r,float b,float red=.045f,float green=.078f,float blue=.068f) {
        glDisable(GL_TEXTURE_2D);glColor4f(red,green,blue,1);glBegin(GL_QUADS);
        glVertex2f(l,t);glVertex2f(r,t);glVertex2f(r,b);glVertex2f(l,b);glEnd();
        glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
    }
    static void frame(float l,float t,float r,float b) {
        glDisable(GL_TEXTURE_2D);glColor4f(.48f,.42f,.20f,1);
        for(float inset:{0.f,3.f}){glBegin(GL_LINE_LOOP);
            glVertex2f(l+inset,t+inset);glVertex2f(r-inset,t+inset);
            glVertex2f(r-inset,b-inset);glVertex2f(l+inset,b-inset);glEnd();}
        glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
    }
};
}
