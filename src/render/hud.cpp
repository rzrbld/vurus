#include "render/hud.h"
#include "assets/native_font_text.h"
#include "render/level_map.h"
#include "game/radar.h"
#include "game/hud_stats.h"
#include "game/properties.h"
#include "game/controls.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace vyruz {
bool HudRenderer::load(const ContentReader& read,const std::string& world,std::string& error) {
    read_=read;
    const auto load=[&](Bank& bank,const std::string& path,bool fonts=false) {
        std::vector<uint8_t> bytes;
        if (!read(world+"/DATA/"+path,bytes) && !read("COMMON/DATA/"+path,bytes)) {
            error="missing HUD resource: "+path;return false;
        }
        if (!decodeGafAnimation(bytes,bank.animation) ||
            (fonts && (!decodeGafFonts(bytes,bank.animation.frames.size(),fonts_) || fonts_.empty()))) {
            error="invalid HUD resource: "+path;return false;
        }
        bank.textures.resize(bank.animation.frames.size());return true;
    };
    if(!load(vPanel_,"PANEL/V-PANEL.GAF") || !load(menu_,"MENU/MENU.GAF") || !load(vMenu_,"MENU/V-MENU.GAF") || !load(panel_,"PANEL/PANEL.GAF",true) || !load(pause_,"MENU/PAUSE.GAF") || !load(failed_,"EXTRA/FAILED.GAF") || !load(inventory_,"MENU/V-INVENTORY.GAF"))return false;
    if(!load(coins_,"EXTRA/COINS.GAF") || !load(score_,"EXTRA/SCORE.GAF") || !load(cursor_,"MENU/CURSOR.GAF") || !load(bonus_,"EXTRA/V-BONUS.GAF"))return false;
    if(!load(surprise_,"OBJECT/ITEM/SURPRISE.GAF") || !load(darkness_,"OBJECT/FX/DARKZONE.GAF") ||
       !load(surpriseFx_,"OBJECT/FX/CIRCULAR-FX.GAF"))return false;
    std::vector<uint8_t> bonusBytes;
    if((!read(world+"/DATA/EXTRA/V-BONUS.GAF",bonusBytes) && !read("COMMON/DATA/EXTRA/V-BONUS.GAF",bonusBytes)) ||
       !decodeGafFonts(bonusBytes,bonus_.animation.frames.size(),bonusFonts_) || bonusFonts_.size()<6 ||
       cursor_.animation.sequences.empty()) {
        error="invalid notification font or cursor";return false;
    }
    std::vector<uint8_t> inventoryBytes;
    if((!read(world+"/DATA/MENU/V-INVENTORY.GAF",inventoryBytes) && !read("COMMON/DATA/MENU/V-INVENTORY.GAF",inventoryBytes)) ||
        !decodeGafFonts(inventoryBytes,inventory_.animation.frames.size(),inventoryFonts_) || inventoryFonts_.empty()) {
        error="invalid inventory font";return false;
    }
    std::vector<uint8_t> menuBytes;
    if((!read(world+"/DATA/MENU/V-MENU.GAF",menuBytes) && !read("COMMON/DATA/MENU/V-MENU.GAF",menuBytes)) ||
        !decodeGafFonts(menuBytes,vMenu_.animation.frames.size(),menuFonts_) || menuFonts_.size()<4) {
        error="invalid menu font";return false;
    }
    constexpr const char* names[]={"GATLING","GRENADE","ROCKET","FIRE","SAW","MISSILE","LASER","PULSAR","MINE"};
    for(size_t i=0;i<weaponIcons_.size();++i) {
        if(!load(weaponIcons_[i],std::string("OBJECT/ITEM/ICON-")+names[i]+".GAF"))return false;
        if(weaponIcons_[i].animation.sequences.empty()){error="weapon icon has no animation";return false;}
        iconCursors_[i].reset(weaponIcons_[i].animation.sequences.front());
    }
    if(menu_.animation.sequences.size()<8 || menu_.animation.sequences[7].subframes.empty()) {
        error="invalid menu cursor";return false;
    }
    menuCursor_.reset(menu_.animation.sequences[7]);
    for(size_t i=0;i<panelCursors_.size() && i<panel_.animation.sequences.size();++i)panelCursors_[i].reset(panel_.animation.sequences[i]);
    for(size_t i=0;i<2;++i)if(bonus_.animation.sequences.size()>i+1)arrowCursors_[i].reset(bonus_.animation.sequences[i+1]);
    return true;
}
void HudRenderer::beginDemoOverlay(){coinsRemainingMs_=500;coinsHidden_=false;}
void HudRenderer::advanceDemoOverlay(double ms){
    if(!(ms>0)||!std::isfinite(ms))return;
    //4457f0 ->431530(500,0), strict comparison/discard overshoot in4315f0.
    if(coinsRemainingMs_<ms){coinsHidden_=!coinsHidden_;coinsRemainingMs_=500;}
    else coinsRemainingMs_-=ms;
}
void HudRenderer::drawDemoOverlay(Gfx& gfx){
    if(coinsHidden_)return;
    const auto view=gfx.viewport();
    //4457f0 creates sequences2 then1; graphical list draws newest first.
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);
    sequence(gfx,coins_,1,view.width/2.f,view.height/2.f);
    glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
    sequence(gfx,coins_,2,view.width/2.f,view.height/2.f);
    glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void HudRenderer::drawScreenBorders(Gfx& gfx) {
    //4350ce..4352e2: TV Borders (detail bit20), V-PANEL7/9/10/8.
    //Stretch only the long axis; original hot spots give 16px inward bands.
    const auto view=gfx.viewport();
    glPushMatrix();glLoadIdentity();glColor4f(1,1,1,1);
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);
    glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR); // Native opcode8 shadow.
    for(size_t number:{7,9,10,8}){
        if(number>vPanel_.animation.sequences.size())continue;
        const auto& sequence=vPanel_.animation.sequences[number-1];if(sequence.subframes.empty())continue;
        const auto& pose=sequence.subframes.front();if(pose.frameIndex>=vPanel_.animation.frames.size())continue;
        const auto& frame=vPanel_.animation.frames[pose.frameIndex];const auto& image=frame.image;
        if(!image.width||!image.height)continue;
        const bool horizontal=number==7||number==8;
        const float sx=horizontal?view.width/image.width:1.f,sy=horizontal?1.f:view.height/image.height;
        const float x=(number==10?view.width-1:0)+(pose.offsetX+frame.originX)*sx;
        const float y=(number==8?view.height-1:0)+(pose.offsetY+frame.originY)*sy;
        auto& texture=vPanel_.textures[pose.frameIndex];if(!texture)texture=gfx.uploadTexture(image.rgba.data(),image.width,image.height);
        gfx.beginQuads(texture);gfx.quad(x,y,0,0,x+image.width*sx,y,1,0,x+image.width*sx,y+image.height*sy,1,1,x,y+image.height*sy,0,1);gfx.endQuads();
    }
    glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glPopMatrix();
}
void HudRenderer::drawDeathPrompt(Gfx& gfx) {
    const auto view=gfx.viewport();
    // 4457f0 registers2 then1;4315f0 draws newest first. Static authored poses.
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);
    sequence(gfx,failed_,1,view.width/2.f,view.height/2.f);
    glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
    sequence(gfx,failed_,2,view.width/2.f,view.height/2.f);
    glDisable(GL_ALPHA_TEST);
    // 460160 hardware gamma: R += (255-R)*80/256; G/B unchanged.
    // Apply to the complete frame without changing the desktop gamma ramp.
    glDisable(GL_TEXTURE_2D);glBlendFunc(GL_ONE_MINUS_DST_COLOR,GL_ONE);
    glColor4f(80.f/256.f,0,0,1);
    glBegin(GL_QUADS);glVertex2f(0,0);glVertex2f(view.width,0);
    glVertex2f(view.width,view.height);glVertex2f(0,view.height);glEnd();
    glColor4f(1,1,1,1);glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void HudRenderer::setLevel(const Level& level) {
    markersReady_=false;markers_={};
    levelMap_=makeLevelMap(level);mapWidth_=level.mapW*level.tileW;mapHeight_=level.mapH*level.tileH;
}
void HudRenderer::clear(Gfx& gfx) {
    gfx.deleteTexture(mapTexture_);mapTexture_=0;levelMap_={};
    for (auto* b:{&coins_,&score_,&panel_,&vPanel_,&pause_,&failed_,&inventory_,&menu_,&vMenu_,&cursor_,&bonus_,&surprise_,&darkness_,&surpriseFx_}) {for(auto t:b->textures)gfx.deleteTexture(t);*b={};}
    for(auto& b:weaponIcons_) {for(auto t:b.textures)gfx.deleteTexture(t);b={};}
    for(auto& [id,bank]:heldBanks_)for(auto t:bank.textures)gfx.deleteTexture(t);
    heldIcons_.clear();heldBanks_.clear();read_={};markers_={};markersReady_=false;
    fonts_.clear();inventoryFonts_.clear();menuFonts_.clear();bonusFonts_.clear();
    menuEffects_={};menuCursor_={};
}
void HudRenderer::drawSurpriseStatus(Gfx& gfx,const GameWorld& world,float width,float height) {
    if(!world.player().alive)return;
    const auto& effect=world.surprise();
    const auto icon=surpriseIcon(effect.kind);if(!icon)return;
    if(icon<=surprise_.animation.sequences.size()) {
        const auto& animation=surprise_.animation.sequences[icon-1];
        AnimationCursor cursor;cursor.reset(animation);
        cursor.advance(animation,std::max(0.f,500.f-effect.delayMs+surpriseDuration(effect.kind)-effect.remainingMs));
        sequence(gfx,surprise_,icon,width-80,height-54,1,cursor.frame(animation));
    }
    if(effect.applied) {
        const int seconds=std::max(0,int(effect.remainingMs))/1000;
        char time[16];std::snprintf(time,sizeof(time),"%d:%02d",seconds/60,seconds%60);
        text(gfx,time,width-100,height-30,2);
    }
}
void HudRenderer::drawSurpriseWorld(Gfx& gfx,const GameWorld& world,float cameraX,float cameraY) {
    const auto& e=world.surprise();
    const auto view=gfx.viewport();
    if(e.feedbackMs>0 && e.feedbackStrength>0) {
        glDisable(GL_TEXTURE_2D);glBlendFunc(GL_ONE,GL_ONE);
        const float flash=e.feedbackStrength/255.f;glColor4f(flash,flash,flash,1);
        glBegin(GL_QUADS);glVertex2f(0,0);glVertex2f(view.width,0);glVertex2f(view.width,view.height);glVertex2f(0,view.height);glEnd();
        glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    if(!e.applied || !world.player().alive)return;
    const auto p=world.playerPosition();
    const float px=p.x-cameraX,py=p.y-cameraY;
    if(e.kind==SurpriseKind::Darkness && !darkness_.animation.frames.empty()) {
        const auto& frame=darkness_.animation.frames.front();
        auto& tex=darkness_.textures.front();
        if(!tex)tex=gfx.uploadTexture(frame.image.rgba.data(),frame.image.width,frame.image.height);
        // Original 128x103 quadrant stretched to half the 376x309 bounds.
        const auto native=nativeHeading(world.player().direction);
        const auto heading=nativeToSurface(native.x,native.y);
        const float cx=px-std::trunc(heading.x*-1700.f)-6,
                    cy=py-std::trunc(heading.y*-2300.f)-14,w=188,h=154.5f;
        const auto fill=[&](float x,float y,float x1,float y1) {
            glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x1,y);glVertex2f(x1,y1);glVertex2f(x,y1);glEnd();
        };
        glDisable(GL_TEXTURE_2D);glColor4f(0,0,0,1);
        fill(0,0,view.width,std::max(0.f,cy-h));fill(0,std::min(float(view.height),cy+h),view.width,view.height);
        fill(0,cy-h,std::max(0.f,cx-w),cy+h);fill(std::min(float(view.width),cx+w),cy-h,view.width,cy+h);
        glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
        gfx.beginQuads(tex);
        for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
            const float x0=cx+(x-1)*w,y0=cy+(y-1)*h;
            gfx.quad(x0,y0,x,y,x0+w,y0,1-x,y,x0+w,y0+h,1-x,1-y,x0,y0+h,x,1-y);
        }
        gfx.endQuads();glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    if(e.kind==SurpriseKind::Xtc)gfx.distortScanlines(std::fmod(xtcPhase_,float(view.height)));
}

void HudRenderer::drawPlayerIndicator(Gfx& gfx,float x,float y,int heading) {
    // 43e5f0: V-BONUS sequence1 at player y-32, rotated 15deg per heading.
    // Its command4/value850000 specifies a dark-red diffuse tint (45eab0).
    if(bonus_.animation.sequences.empty() || bonus_.animation.sequences[0].subframes.empty())return;
    const auto& pose=bonus_.animation.sequences[0].subframes[0];
    const uint32_t color=(pose.command&4)?pose.sample:0xffffff;
    glPushMatrix();glTranslatef(x,y-32,0);glRotatef((39-((heading%24+24)%24))*15.f,0,0,1);
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ONE,GL_ONE);
    glColor3ub((color>>16)&255,(color>>8)&255,color&255);
    sequence(gfx,bonus_,1,0,0,1,&pose);
    glColor4f(1,1,1,1);glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glPopMatrix();
}

void HudRenderer::drawCursor(Gfx& gfx,float x,float y) {
    // 42f2d0/43e5f0: CURSOR.GAF sequence1 with its authored offset/hotspot.
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ONE,GL_ONE);
    sequence(gfx,cursor_,1,x,y);
    glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
unsigned HudRenderer::advanceMenu(MenuPage page,uint32_t elapsedMs,int refreshHz,bool pointerVisible) {
    const unsigned flashes=menuEffects_.advance(page,elapsedMs,refreshHz);
    // 421780 advances the global MENU8 channel with frame elapsed for every
    // logical menu update. Keep its authored phase through submenu changes.
    if(pointerVisible && menu_.animation.sequences.size()>=8)
        for(unsigned i=0;i<menuEffects_.updates();++i)
            menuCursor_.advance(menu_.animation.sequences[7],elapsedMs);
    return flashes;
}
void HudRenderer::drawMenuCursor(Gfx& gfx,float x,float y) {
    if(menu_.animation.sequences.size()<8)return;
    const auto* pose=menuCursor_.frame(menu_.animation.sequences[7]);if(!pose)return;
    // Native421780 uses MENU8, keyed copy opcode1, after the moving line.
    // Preserve its offset/hotspot; gameplay's CURSOR.GAF is a separate asset.
    glColor4f(1,1,1,1);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    sequence(gfx,menu_,8,x,y,1,pose);
}
void HudRenderer::drawPresentation(Gfx& gfx,const Presentation& presentation) {
    if(!presentation.open() || presentation.page==PresentationPage::Movie)return;
    if(presentation.page==PresentationPage::EndingTerminal) {
        if(fonts_.size()<3)return;
        const auto& font=fonts_[2];
        for(size_t row=0;row<5;++row) {
            const auto line=presentation.terminalLine(row);float x=100,y=100+20*row;
            glColor4f(1,1,1,1);textBank(gfx,line,x,y,panel_,font,(font.words[8]&255)==7,false,540);
            if(!presentation.terminalCursor(row))continue;
            for(unsigned char c:line) {
                if(c==' '){x+=font.fixedAdvance();continue;}
                const auto* glyph=font.glyph(c);uint32_t width=0;
                if(glyph && glyph->layers[0].frameIndex<panel_.animation.frames.size())width=panel_.animation.frames[glyph->layers[0].frameIndex].image.width;
                x+=font.advance(width);
            }
            glDisable(GL_TEXTURE_2D);glColor3ub(0,255,0);glBegin(GL_QUADS);
            glVertex2f(x,y-4);glVertex2f(x+4,y-4);glVertex2f(x+4,y);glVertex2f(x,y);glEnd();glEnable(GL_TEXTURE_2D);
        }
        glColor4f(1,1,1,1);return;
    }
    const bool mission=presentation.page==PresentationPage::Mission;
    if(mission) {
        // 439a20/4282a0: V-INVENTORY sequence1 tiles, inset(32,16), keyed shadow.
        glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
        for(int y=16;y+64<480;y+=64)for(int x=32;x+64<640;x+=64)sequence(gfx,inventory_,1,x,y);
        glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    const auto& font=bonusFonts_[mission?2:3];
    const auto diffuse=font.hasAuthoredStyle()?font.words[10]:0xffffff;
    const auto specular=font.hasAuthoredStyle()?font.words[11]:0;
    const float startX=mission?64:presentation.page==PresentationPage::Ending?70:presentation.page==PresentationPage::EndingSecond?140:128;
    float x=startX,y=mission?80:100+static_cast<int32_t>(font.words[5]);
    const auto write=[&](std::string_view span) {
        glColor3ub((diffuse>>16)&255,(diffuse>>8)&255,diffuse&255);
        textBank(gfx,span,x,y,bonus_,font,(font.words[8]&255)==7,false,640-x);
        if(specular) {
            // Constant specular contribution on non-key glyph pixels, after diffuse.
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);
            glColor3ub((specular>>16)&255,(specular>>8)&255,specular&255);
            textBank(gfx,span,x,y,bonus_,font,true,false,640-x);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        }
    };
    const auto value=presentation.visibleText();
    size_t start=0;
    for(size_t i=0;i<=value.size();++i) {
        if(i<value.size() && value[i]!='\n' && value[i]!='\t')continue;
        const auto span=value.substr(start,i-start);write(span);
        for(unsigned char c:span) {
            if(c==' '){x+=font.fixedAdvance();continue;}
            const auto* glyph=font.glyph(c);uint32_t width=0;
            if(glyph && glyph->layers[0].frameIndex<bonus_.animation.frames.size())width=bonus_.animation.frames[glyph->layers[0].frameIndex].image.width;
            x+=font.advance(width);
        }
        if(i<value.size() && value[i]=='\n'){x=startX;y+=mission?25:font.lineAdvance();}
        else if(i<value.size()){const int tab=mission?64:8*font.fixedAdvance();x=std::floor(x/tab)*tab+tab;}
        start=i+1;
    }
    glColor4f(1,1,1,1);
}
void HudRenderer::drawNotifications(Gfx& gfx,const GameWorld& world,float camX,float camY) {
    const auto layout=gfx.hudLayout();
    const auto draw=[&](const Notification& notice) {
        auto& bank=notice.panelFont?panel_:bonus_;
        const auto& fonts=notice.panelFont?fonts_:bonusFonts_;
        if(notice.font==0 || notice.font>fonts.size())return;
        const auto& font=fonts[notice.font-1];
        float x=notice.x,y=notice.currentY();
        if(notice.worldSpace) {x-=camX;y-=camY;}
        else {
            glPushMatrix();glTranslatef(layout.x,layout.y,0);glScalef(layout.scale,layout.scale,1);
            if(notice.panelFont) {
                if(notice.kind==NoticeKind::WeaponInfo)x+=layout.width-640;
            } else {
                x+=(layout.width-640)/2;
                y+=(layout.height-480)/(notice.kind==NoticeKind::LevelName?1:2);
            }
        }
        // 4315f0 selects the font AFTER the requested draw operation. 44c3c0
        // restores authored mode: V-BONUS font2 keyed copy, font6 keyed additive.
        glColor3ub((notice.color>>16)&255,(notice.color>>8)&255,notice.color&255);
        textBank(gfx,notice.visibleText(),x,y,bank,font,(font.words[8]&255)==7,false,10000);
        if(!notice.worldSpace)glPopMatrix();
        glColor4f(1,1,1,1);
    };
    for(const auto& price:world.notifications().savingPrices())
        if(price.active && !price.hidden && price.notice.ageMs<price.notice.lifetimeMs)draw(price.notice);
    for(const auto& notice:world.notifications().entries())draw(notice);
}
void HudRenderer::advance(double ms,const GameWorld* world) {
    if(world && world->playerIndex()<world->actors().size()) {
        const bool active=world->surprise().active(SurpriseKind::Xtc);
        const float y=world->playerPosition().y;
        if(active) {
            if(!xtcActive_) {xtcPhase_=0;xtcY_=y;}
            xtcPhase_+=std::trunc(y-xtcY_)+1;
        }
        xtcY_=y;xtcActive_=active;
    }
    if(!world)return;
    const auto selected=world->player().selectedWeapon;
    if(selected<weaponIcons_.size() && !weaponIcons_[selected].animation.sequences.empty())
        iconCursors_[selected].advance(weaponIcons_[selected].animation.sequences.front(),ms);
    const auto advancePanel=[&](size_t sequence) {
        if(sequence<=panel_.animation.sequences.size())panelCursors_[sequence-1].advance(panel_.animation.sequences[sequence-1],ms);
    };
    advancePanel(15);
    //435c3c/435cd9: only the current locked cycle advances PANEL13. Losing
    //lock preserves its phase; static first-frame underlay is drawn below.
    if(world->playerLocked())advancePanel(13);
    if(world->player().vitals.shieldMax>0 && static_cast<long double>(world->player().vitals.shield)/world->player().vitals.shieldMax<=static_cast<long double>(.01f))advancePanel(1);
    advancePanel(world->surprise().active(SurpriseKind::Pinball) && world->player().contactConfig.elasticity!=1.5f?4:5);
    if(world->player().weapons.empty())advancePanel(6);
    else {
        const auto stock=weaponStock(world->player(),world->player().weapons[selected]);
        if(stock==world->player().ammo.end() || !stock->second.count)advancePanel(6);
    }
    syncInventory(*world);
}
void HudRenderer::bitmap(Gfx& gfx,Bank& bank,size_t id,float x,float y,float clip) {
    if(id>=bank.animation.frames.size())return;
    clip=std::clamp(clip,0.f,1.f);if(clip==0)return;
    const auto& image=bank.animation.frames[id].image;
    const float w=std::floor(image.width*clip),h=image.height;
    if(!w||!h)return;
    auto& texture=bank.textures[id];
    if(!texture)texture=gfx.uploadTexture(image.rgba.data(),image.width,image.height);
    gfx.beginQuads(texture);
    const float u=w/image.width;
    gfx.quad(x,y,0,0,x+w,y,u,0,x+w,y+h,u,1,x,y+h,0,1);gfx.endQuads();
}
void HudRenderer::sequence(Gfx& gfx,Bank& bank,size_t number,float x,float y,float clip,const GafSubframe* selected) {
    if(number==0||number>bank.animation.sequences.size())return;
    const auto& s=bank.animation.sequences[number-1];if(s.subframes.empty())return;
    const auto& pose=selected?*selected:s.subframes.front();if(pose.frameIndex>=bank.animation.frames.size())return;
    const auto& frame=bank.animation.frames[pose.frameIndex];
    bitmap(gfx,bank,pose.frameIndex,x+pose.offsetX+frame.originX,y+pose.offsetY+frame.originY,clip);
}
void HudRenderer::text(Gfx& gfx,std::string_view value,float x,float y,size_t fontIndex,bool inventory,float maxWidth) {
    const auto& fonts=inventory?inventoryFonts_:fonts_;
    auto& bank=inventory?inventory_:panel_;
    if(fonts.empty())return;
    const auto& font=fonts[std::min(fontIndex,fonts.size()-1)];
    textBank(gfx,value,x,y,bank,font,inventory,!inventory,maxWidth);
}
void HudRenderer::textBank(Gfx& gfx,std::string_view value,float x,float y,Bank& bank,const GafFont& font,
                           bool additive,bool uppercase,float maxWidth) {
    const float start=x;
    if(additive){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ONE,GL_ONE);}
    for(unsigned char c:value) {
        if(uppercase && c>='a' && c<='z') c-='a'-'A';
        const auto parsed=nativeFontByte(c);
        if(parsed.kind==NativeFontByteKind::Stop)break;
        if(parsed.kind==NativeFontByteKind::Newline){x=start;y+=font.lineAdvance();continue;}
        if(parsed.kind==NativeFontByteKind::Ignore)continue;
        if(parsed.kind==NativeFontByteKind::Tab){x=float(nativeFontTabX(static_cast<int64_t>(x),font.fixedAdvance()));continue;}
        if(parsed.kind==NativeFontByteKind::Space){x+=font.fixedAdvance();continue;}
        const auto* glyph=font.glyph(parsed.glyph);uint32_t width=0;
        if(glyph && glyph->layers[0].frameIndex<bank.animation.frames.size())
            width=bank.animation.frames[glyph->layers[0].frameIndex].image.width;
        if(x-start+std::max<int64_t>(width,font.advance(width))>maxWidth)break;
        if(glyph && glyph->layers[0].frameIndex<bank.animation.frames.size())for(size_t i=0;i<glyph->layers.size();++i) {
            const auto& l=glyph->layers[i];if(l.frameIndex>=bank.animation.frames.size())continue;
            // Font offsets cancel the bitmap hotspot (44ba30).
            bitmap(gfx,bank,l.frameIndex,x+l.offsetX,y+l.offsetY);
            if(i==0)width=bank.animation.frames[l.frameIndex].image.width;
        }
        x+=font.advance(width);
    }
    if(additive){glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
}
void HudRenderer::draw(Gfx& gfx,const GameWorld& world,const SceneContent& scene,bool paused,float w,float h) {
    const auto& p=world.player();
    const auto ratio=[](float current,float maximum){return maximum>0?std::clamp(current/maximum,0.f,1.f):0.f;};
    if(world.mapOpen() && p.alive && !levelMap_.rgba.empty()) {
        if(!mapTexture_)mapTexture_=gfx.uploadTexture(levelMap_.rgba.data(),levelMap_.width,levelMap_.height);
        gfx.beginQuads(mapTexture_);gfx.quad(0,0,0,0,w,0,1,0,w,h,1,1,0,h,0,1);gfx.endQuads();
        if(!markersReady_)rebuildMapMarkers(scene,world);
        const auto marker=[&](size_t sequenceId,ContentPosition pos) {
            sequence(gfx,panel_,sequenceId,mapMarkerCoordinate(pos.x,w,mapWidth_),mapMarkerCoordinate(pos.y,h,mapHeight_));
        };
        marker(21,world.playerPosition());
        for(auto it=markers_.lifts.rbegin();it!=markers_.lifts.rend();++it)if(*it<scene.objects.size())marker(language_==2?24:23,scene.objects[*it].position);
        for(auto it=markers_.saves.rbegin();it!=markers_.saves.rend();++it)if(*it<scene.objects.size())marker(language_==2?26:25,scene.objects[*it].position);
        text(gfx,"MAP   CREDITS "+std::to_string(p.vitals.money)+"   M/ESC - CLOSE",12,18,2);
    } else {
    const auto panelPose=[&](size_t id){return panelCursors_[id-1].frame(panel_.animation.sequences[id-1]);};
    const bool lowShield=p.vitals.shieldMax>0 && static_cast<long double>(p.vitals.shield)/p.vitals.shieldMax<=static_cast<long double>(.01f);
    sequence(gfx,panel_,1,w/2.f,0);
    if(lowShield)if(const auto* pose=panelPose(1))sequence(gfx,panel_,1,w/2.f,0,1,pose);
    sequence(gfx,panel_,11,w/2.f,0,ratio(p.vitals.shield,p.vitals.shieldMax));
    sequence(gfx,panel_,12,w/2.f,0,ratio(p.vitals.energy,p.vitals.energyMax));
    sequence(gfx,panel_,2,w-1.f,0);
    const size_t indicator=world.surprise().active(SurpriseKind::Pinball) && p.contactConfig.elasticity!=1.5f?4:5;
    sequence(gfx,panel_,indicator,w-1.f,0,1,panelPose(indicator));
    sequence(gfx,panel_,15,0,0);
    if(const auto* pose=panelPose(15))sequence(gfx,panel_,15,0,0,1,pose);
    sequence(gfx,panel_,16,0,0);
    char score[32];std::snprintf(score,sizeof(score),"%08lld $",static_cast<long long>(p.vitals.money));text(gfx,score,36,30);
    if(p.weapons.empty())sequence(gfx,panel_,6,w-1.f,0,1,panelPose(6));
    else {
        const auto& weapon=p.weapons[p.selectedWeapon];const auto ammo=weaponStock(p,weapon);
        if(ammo==p.ammo.end() || !ammo->second.count)sequence(gfx,panel_,6,w-1.f,0,1,panelPose(6));
        text(gfx,std::to_string(ammo==p.ammo.end()?0:ammo->second.count),w-108.f,9);
        if(p.selectedWeapon<weaponIcons_.size()) {
            auto& bank=weaponIcons_[p.selectedWeapon];
            const auto* pose=iconCursors_[p.selectedWeapon].frame(bank.animation.sequences.front());
            if(pose)sequence(gfx,bank,1,w-61.f,70,1,pose);
        }
    }
    sequence(gfx,panel_,13,w-1.f,h-1.f);
    if(world.playerLocked())if(const auto* pose=panelPose(13))sequence(gfx,panel_,13,w-1.f,h-1.f,1,pose);
    sequence(gfx,panel_,14,w-1.f,h-1.f);
    sequence(gfx,vPanel_,3,w-1.f,h-1.f);
    if(!world.surprise().applied) {
    const auto position=world.playerPosition();
    char coordinate[32];std::snprintf(coordinate,sizeof(coordinate),"%04d",static_cast<int>(position.x));
    text(gfx,coordinate,w-108.f,h-71.f,2);
    std::snprintf(coordinate,sizeof(coordinate),"%04d",static_cast<int>(position.y));
    text(gfx,coordinate,w-73.f,h-71.f,2);
    text(gfx,std::string(language_==2?"TUES ":"KILLED ")+std::to_string(world.playerKills()),w-108.f,h-61.f,2);
    text(gfx,std::string(language_==2?"ENNEMIS ":"ENEMY ")+std::to_string(world.enemiesRemaining()),w-108.f,h-51.f,2);
    const auto seconds=static_cast<uint64_t>(world.elapsedMilliseconds()/1000);
    char clock[64];std::snprintf(clock,sizeof(clock),"%02llu:%02llu:%02llu",
        static_cast<unsigned long long>(seconds/3600),static_cast<unsigned long long>(seconds/60%60),static_cast<unsigned long long>(seconds%60));
    text(gfx,clock,w-108.f,h-41.f,2);
    }
    sequence(gfx,panel_,17,0,h-1.f);
    sequence(gfx,panel_,18,0,h-1.f);
    sequence(gfx,panel_,19,0,h-1.f);
    // Movement currently uses a port surface-pixels/second scale.
    char speed[16];std::snprintf(speed,sizeof(speed),"%03d",nativeSpeedReadout(world.playerNativeSpeed()));
    text(gfx,speed,105,h-5.f,1);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    const auto& actors=world.actors();
    const auto& cells=world.nativeActorCells();
    const auto center=world.playerPosition();
    const int x0=std::clamp(int(std::floor((center.x-960)/64)),0,cells.width),x1=std::clamp(int(std::floor((center.x+960)/64))+1,0,cells.width);
    const int y0=std::clamp(int(std::floor((center.y-960)/32)),0,cells.height),y1=std::clamp(int(std::floor((center.y+960)/32))+1,0,cells.height);
    for(int x=x0;x<x1;++x)for(int y=y0;y<y1;++y)
    for(size_t i=cells.heads[size_t(x)*cells.height+y];i!=NativeActorCells::Missing;i=cells.next[i]) {
        if(i>=actors.size() || i>=scene.objects.size())continue;
        const auto blip=radarBlip(scene.objects[i],actors[i].alive,i==world.playerIndex(),world.playerPosition(),h);
        if(!blip)continue;
        switch(blip->kind) {
            case RadarKind::Enemy:glColor3ub(255,0,0);break;
            case RadarKind::Player:glColor3ub(0,255,0);break;
            case RadarKind::Key:glColor3ub(255,255,0);break;
            case RadarKind::Lift:glColor3ub(0,0,255);break;
        }
        const int x=blip->x,y=blip->y;
        glVertex2i(x,y);glVertex2i(x+1,y);glVertex2i(x+1,y+1);glVertex2i(x,y+1);
    }
    glEnd();glColor4f(1,1,1,1);glEnable(GL_TEXTURE_2D);
    }
    if(world.debugGodMode() || !world.debugEnemyAI())
        text(gfx,std::string("DEBUG  ")+(world.debugGodMode()?"GOD ON  ":"")+(!world.debugEnemyAI()?"ENEMY AI OFF":""),16,52,2);
    if(p.alive && !world.message().empty())text(gfx,world.message().substr(0,75),16,h-20.f,2);
    if(p.alive && paused) {
        if(!pause_.animation.frames.empty()) {
            const auto& image=pause_.animation.frames.front().image;
            glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ONE,GL_ONE);
            bitmap(gfx,pause_,0,(w-image.width)/2.f,(h-image.height)/2.f);
            glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        }
        text(gfx,"P - RESUME    ESC - MENU",w/2.f-92,h-24.f,2);
    }
}
}

namespace vyruz {
void HudRenderer::drawControlsHelp(Gfx& gfx,const std::vector<std::pair<std::string,int>>& rows,int first) {
    // 43ab50: centered V-INVENTORY tiled modal; 40px rows/font1/weapon icons.
    constexpr int left=32,top=48,width=576,height=384,visible=height/40;
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
    for(int y=0;y<height;y+=64)for(int x=0;x<width;x+=64)sequence(gfx,inventory_,1,left+x,top+y);
    glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,1,1,1);
    for(int i=0;i<visible&&first+i<int(rows.size());++i){
        const auto& [label,slot]=rows[first+i];const float y=top+40+i*40;
        if(slot>=0&&slot<int(weaponIcons_.size()))sequence(gfx,weaponIcons_[slot],1,left+32,y+8);
        text(gfx,label,left+82,y,0,true,width-114);
    }
    if(first&&bonus_.animation.sequences.size()>=2)sequence(gfx,bonus_,2,left+width-16,top+16);
    if(first+visible<int(rows.size())&&bonus_.animation.sequences.size()>=3)sequence(gfx,bonus_,3,left+width-16,top+height-16);
    text(gfx,"UP/DOWN OR WHEEL - SCROLL    ESC/SPACE/RETURN - CLOSE",left+32,top+height+12,2);
}
void HudRenderer::drawInventory(Gfx& gfx,const GameWorld& world,const InventoryView& view,int w,int h,std::string_view closeKey) {
    if(!view.open)return;
    constexpr int tile=64;
    const int width=(w-tile)/tile*tile,height=(h-tile)/tile*tile;
    const int left=(w-width)/2,top=(h-height)/2;
    glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
    for(int y=0;y<height;y+=tile)for(int x=0;x<width;x+=tile)sequence(gfx,inventory_,1,left+x,top+y);
    glDisable(GL_ALPHA_TEST);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    text(gfx,"INVENTORY",left+32,top+18,2);
    syncInventory(world);
    const auto rows=inventoryRows(world.player(),language_);
    const size_t visible=static_cast<size_t>(std::max(0,height/40));
    for(size_t i=0;i<visible && view.firstRow+i<rows.size();++i) {
        const auto& row=rows[view.firstRow+i];if(row.separator)continue;
        const float x=left+32,y=top+40+i*40;
        if(row.weaponSlot>=0 && static_cast<size_t>(row.weaponSlot)<weaponIcons_.size()) {
            auto& bank=weaponIcons_[row.weaponSlot];
            const auto* pose=iconCursors_[row.weaponSlot].frame(bank.animation.sequences.front());
            if(pose)sequence(gfx,bank,1,x,y+8,1,pose);
        }
        if(row.icon) {
            const auto found=heldIcons_.find(heldIconId(*row.icon));
            if(found!=heldIcons_.end()) {
                auto& icon=found->second;auto& bank=heldBanks_.at(icon.path);
                const auto* pose=icon.cursor.frame(bank.animation.sequences[icon.sequence-1]);
                if(pose) {
                    const auto opcode=pose->flags&255;
                    if(opcode==7){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);glBlendFunc(GL_ONE,GL_ONE);}
                    sequence(gfx,bank,icon.sequence,x,y+8,1,pose);
                    if(opcode==7)glDisable(GL_ALPHA_TEST);
                    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
                }
            }
        }
        text(gfx,row.label,x+50,y,0,true,width-114.f);
    }
    if(view.firstRow && bonus_.animation.sequences.size()>=2)sequence(gfx,bonus_,2,left+width-16,top+16,1,arrowCursors_[0].frame(bonus_.animation.sequences[1]));
    if(view.firstRow+visible<rows.size() && bonus_.animation.sequences.size()>=3)sequence(gfx,bonus_,3,left+width-16,top+height-16,1,arrowCursors_[1].frame(bonus_.animation.sequences[2]));
    text(gfx,std::string("UP/DOWN OR WHEEL - SCROLL     ")+std::string(closeKey)+"/ESC - CLOSE",left+32,top+height+12,2);
    text(gfx,std::to_string(std::min(rows.size(),view.firstRow+1))+"/"+std::to_string(rows.size()),left+width-55,top+height+12,2);
}
}

namespace vyruz {
float HudRenderer::drawMenuText(Gfx& gfx,std::string_view value,float x,float y,unsigned fontIndex,float width,bool wrap) {
    if(!fontIndex || fontIndex>menuFonts_.size())return 0;
    for(size_t i=0;i<value.size();++i)if(nativeFontByte(static_cast<uint8_t>(value[i])).kind==NativeFontByteKind::Stop){value=value.substr(0,i);break;}
    const auto& font=menuFonts_[fontIndex-1];
    const auto advance=[&](unsigned char c){const auto parsed=nativeFontByte(c);if(parsed.kind==NativeFontByteKind::Space)return float(font.fixedAdvance());const auto* glyph=font.glyph(parsed.glyph);uint32_t w=0;
        if(glyph&&glyph->layers[0].frameIndex<vMenu_.animation.frames.size())w=vMenu_.animation.frames[glyph->layers[0].frameIndex].image.width;
        return float(font.advance(w));};
    std::vector<std::string> lines(1);float used=0;
    for(size_t i=0;i<value.size();){const unsigned char c=value[i];if(nativeFontByte(c).kind==NativeFontByteKind::Stop)break;if(c=='\r'){++i;continue;}if(c=='\n'){lines.emplace_back();used=0;++i;continue;}
        size_t end=i+1;if(c!=' '&&c!='\t')while(end<value.size()&&value[end]!=' '&&value[end]!='\t'&&value[end]!='\r'&&value[end]!='\n')++end;
        float wordWidth=0;for(size_t j=i;j<end;++j)wordWidth+=value[j]=='\t'?float(nativeFontTabX(static_cast<int64_t>(x+used+wordWidth),font.fixedAdvance()))-(x+used+wordWidth):advance(value[j]);
        if(wrap&&used>0&&c!=' '&&used+wordWidth>width){lines.emplace_back();used=0;}
        for(;i<end;++i){const char ch=value[i];const float step=ch=='\t'?float(nativeFontTabX(static_cast<int64_t>(x+used),font.fixedAdvance()))-(x+used):advance(ch);
            if(wrap&&used>0&&used+step>width){lines.emplace_back();used=0;if(ch==' ')continue;}
            lines.back().push_back(ch);used+=step;}
    }
    const auto diffuse=font.hasAuthoredStyle()?font.words[10]:0xffffff;
    const auto specular=font.hasAuthoredStyle()?font.words[11]:0;
    for(size_t row=0;row<lines.size();++row){const float baseline=y+row*font.lineAdvance();
        glColor3ub((diffuse>>16)&255,(diffuse>>8)&255,diffuse&255);
        textBank(gfx,lines[row],x,baseline,vMenu_,font,(font.words[8]&255)==7,false,width);
        if(specular){glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_REPLACE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);
            glColor3ub((specular>>16)&255,(specular>>8)&255,specular&255);textBank(gfx,lines[row],x,baseline,vMenu_,font,true,false,width);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);}
    }
    glColor4f(1,1,1,1);return lines.size()*font.lineAdvance();
}
void HudRenderer::drawHighScores(Gfx& gfx,const HighScores& scores) {
    if(menuFonts_.size()<4)return;
    glDisable(GL_BLEND);sequence(gfx,score_,1,0,0);glEnable(GL_BLEND);
    const auto& font=menuFonts_[3];
    const auto measure=[&](std::string_view text){float total=0;for(unsigned char c:text){if(c==' '){total+=font.fixedAdvance();continue;}
        const auto* glyph=font.glyph(c);uint32_t width=0;if(glyph&&glyph->layers[0].frameIndex<vMenu_.animation.frames.size())width=vMenu_.animation.frames[glyph->layers[0].frameIndex].image.width;total+=font.advance(width);}return total;};
    const auto write=[&](std::string_view text,float x,float y,float width,int align,uint32_t color){const float measured=measure(text);
        if(align==1)x+=(width-measured)*.5f;else if(align==2)x+=width-measured;
        glColor3ub((color>>16)&255,(color>>8)&255,color&255);textBank(gfx,text,x,y,vMenu_,font,true,false,640-x);};
    write("THE BRAVEST RESISTANCE FIGHTERS",0,48,640,1,0x80ffa0);
    constexpr float xs[]={40,80,130,180,220,310,340,410,430,450,490};
    constexpr float widths[]={40,40,40,30,70,20,55,20,20,20,150};
    constexpr int align[]={0,1,1,1,2,1,2,2,1,0,0};
    constexpr const char* labels[]={"Rank","Tower","Level","Killed","Money","Hit~","Ammo","","Time","","Name"};
    for(size_t c=0;c<11;++c)write(labels[c],xs[c],80,widths[c],align[c],0x80ffa0);
    for(size_t row=0;row<scores.size();++row){const auto& entry=scores[row];const unsigned weight=row==0?256:256-unsigned(row*256/19);
        const auto mix=[&](unsigned target){return (255*weight+target*(256-weight))>>8;};const uint32_t color=mix(128)<<16|mix(160)<<8|255;
        std::array<std::string,11> values;values[0]="#"+std::to_string(row+1);for(size_t c=1;c<=6;++c)values[c]=std::to_string(entry.values[c+1]);
        char time[16];std::snprintf(time,sizeof(time),"%02u",entry.values[8]);values[7]=time;std::snprintf(time,sizeof(time),":%02u",entry.values[9]);values[8]=time;std::snprintf(time,sizeof(time),":%02u",entry.values[10]);values[9]=time;values[10]=entry.name;
        for(size_t c=0;c<11;++c)write(values[c],xs[c],110+row*18,widths[c],align[c],color);
    }
    glColor4f(1,1,1,1);
}
void HudRenderer::drawMenu(Gfx& gfx,const MenuView& view,bool backgroundOnly) {
    if(!view.open())return;
    glDisable(GL_BLEND);sequence(gfx,menu_,4,0,0);glEnable(GL_BLEND);
    if(menuEffects_.takeLogo()) {
        // 421780 draws V-MENU6 at (0,0), with authored (-3,-13) offset
        // and unkeyed additive opcode6; the next draw consumes the blink.
        glBlendFunc(GL_ONE,GL_ONE);glColor4f(1,1,1,1);
        sequence(gfx,vMenu_,6,0,0);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    if(backgroundOnly)return;
    const auto write=[&](std::string_view value,float x,float y,size_t font,float width=360.f) {
        drawMenuText(gfx,value,x,y,unsigned(font+1),width);
    };
    const auto rows=view.entries();
    const auto title=view.title()+(view.paginated()&&rows.size()>10?"  "+std::to_string(view.firstRow()/10+1)+" / "+std::to_string((rows.size()+9)/10):"");
    if(view.page==MenuPage::SavedSessions)write(title,10,120,0);
    else if(view.title()=="Extras")write(title,10,120,0);else write(title,270,50,3);
    for(size_t i=view.firstRow();i<rows.size() && i<static_cast<size_t>(view.firstRow()+10);++i)
        write(rows[i].label,view.rowLeft(),view.rowTop()+(i-view.firstRow())*view.rowHeight(),
            view.page==MenuPage::SavedSessions?(i==static_cast<size_t>(view.selected)?3:2):(i==static_cast<size_t>(view.selected)?1:0));
    if(view.page==MenuPage::Controls) {
        write(view.controlsHelp,270,86,3);
    } else if(view.page==MenuPage::Confirm) {
        write("Unsaved progress will be lost.",270,250,3);
    }
    if(view.page==MenuPage::DebugSurprises)write("Enter: replace effect and resume\nUp/Down: browse. Esc: back",270,405,3);
    else if(view.page==MenuPage::DebugLevels)write("Enter: fresh unsaved test session\nDisk saves are unchanged. Esc: back",270,405,3);
    else if(view.page!=MenuPage::Main && view.page!=MenuPage::Extras && view.page!=MenuPage::Options && view.page!=MenuPage::Game && view.page!=MenuPage::SavedSessions)
        write("Arrows: select   Enter: choose\nMouse: point and click   Esc: back",270,435,3);
    // Port options have up to ten rows; reserve their help below the last row.
    const float helpY=view.page==MenuPage::Options&&rows.size()>8?400:360;
    if(menuFonts_.size()>=4) {
        // 421780 positions the first baseline within the help rectangle using
        // authored font height minus the baseline adjustment (19-2=17).
        const auto& font=menuFonts_[3];
        const GLdouble top[]={0,1,0,-helpY},bottom[]={0,-1,0,475};
        glClipPlane(GL_CLIP_PLANE0,top);glClipPlane(GL_CLIP_PLANE1,bottom);
        glEnable(GL_CLIP_PLANE0);glEnable(GL_CLIP_PLANE1);
        drawMenuText(gfx,view.selectedHelp(),400,helpY+float(font.words[7])-float(font.words[2]),4,235,true);
        glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);
    }
    drawMenuText(gfx,"Apr 10 2000 17:52:06",4,465,5,636);
    if(!view.error.empty()) {
        const float statusY=view.page==MenuPage::SavedSessions?335:view.page==MenuPage::Options?(rows.size()>8?385:335):view.paginated()?385:365;
        write(view.error,270,statusY,3);
    }
}
void HudRenderer::drawMenuScanline(Gfx& gfx) {
    // 421050 binds V-MENU1 (not the slider decoration in sequence5).
    // 421780 draws it after page callbacks, stretched across the viewport.
    if(vMenu_.animation.sequences.empty() || vMenu_.animation.sequences[0].subframes.empty())return;
    const auto& pose=vMenu_.animation.sequences[0].subframes[0];
    if(pose.frameIndex>=vMenu_.animation.frames.size())return;
    const auto& frame=vMenu_.animation.frames[pose.frameIndex];
    if(frame.image.width<=0)return;
    const auto view=gfx.viewport();
    glPushMatrix();glTranslatef(-view.uiX(),float(menuEffects_.scanline()),0);
    glScalef(float(view.width)/frame.image.width,1,1);
    glBlendFunc(GL_ONE,GL_ONE);glColor4f(1,1,1,1);
    sequence(gfx,vMenu_,1,0,0);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glPopMatrix();
}
}

namespace vyruz {
std::string HudRenderer::heldIconId(const HeldCode& held) {
    return held.mapPath+":"+std::to_string(held.object.id.value)+":"+held.object.sprite.path+":"+std::to_string(held.object.sprite.sequence.value);
}
std::optional<AnimationCursorState> HudRenderer::inventoryIconCursor(const HeldCode& held) const {
    const auto it=heldIcons_.find(heldIconId(held));
    return it==heldIcons_.end()?std::optional<AnimationCursorState>{}:it->second.cursor.capture();
}
void HudRenderer::syncInventory(const GameWorld& world,const std::function<std::optional<AnimationCursorState>(size_t)>& seed) {
    const auto serial=++inventorySyncSerial_;
    for(const auto& key:world.player().heldKeys)if(key.held) {
        const auto& held=*key.held;const auto id=heldIconId(held);
        if(auto found=heldIcons_.find(id);found!=heldIcons_.end()){found->second.seen=serial;continue;}
        const auto& sprite=held.object.sprite;
        if(!read_ || sprite.path.empty() || !sprite.sequence.value)continue;
        HeldIcon icon;icon.sequence=sprite.sequence.value;icon.path=sprite.path;icon.seen=serial;
        auto found=heldBanks_.find(icon.path);
        if(found==heldBanks_.end()) {
            std::vector<uint8_t> bytes;Bank bank;
            if(!read_(sprite.path,bytes) || !decodeGafAnimation(bytes,bank.animation))continue;
            bank.textures.resize(bank.animation.frames.size());found=heldBanks_.emplace(icon.path,std::move(bank)).first;
        }
        if(icon.sequence>found->second.animation.sequences.size())continue;
        const auto& sequence=found->second.animation.sequences[icon.sequence-1];icon.cursor.reset(sequence);
        if(seed)if(const auto cursor=seed(held.object.id.value))icon.cursor.restore(sequence,*cursor);
        heldIcons_.emplace(id,std::move(icon));
    }
    //A used/expired key may later reactivate at the same source ID. It must
    //seed its new pickup phase instead of reusing an old detached cursor.
    std::erase_if(heldIcons_,[&](const auto& entry){return entry.second.seen!=serial;});
}
void HudRenderer::advanceInventory(double ms,const GameWorld& world,const InventoryView& view) {
    syncInventory(world);
    const auto rows=inventoryRows(world.player(),language_);
    for(size_t i=view.firstRow;i<rows.size();++i) {
        const auto& row=rows[i];
        if(row.weaponSlot>=0 && size_t(row.weaponSlot)<weaponIcons_.size()) {
            auto& bank=weaponIcons_[row.weaponSlot];
            if(!bank.animation.sequences.empty())iconCursors_[row.weaponSlot].advance(bank.animation.sequences[0],ms);
        }
        if(row.icon)if(auto it=heldIcons_.find(heldIconId(*row.icon));it!=heldIcons_.end())
            it->second.cursor.advance(heldBanks_.at(it->second.path).animation.sequences[it->second.sequence-1],ms);
    }
    if(view.firstRow && bonus_.animation.sequences.size()>=2)arrowCursors_[0].advance(bonus_.animation.sequences[1],ms);
    if(view.firstRow+9<rows.size() && bonus_.animation.sequences.size()>=3)arrowCursors_[1].advance(bonus_.animation.sequences[2],ms);
}
}
