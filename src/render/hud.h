#pragma once
#include "assets/gaf_font.h"
#include "assets/gaf_sprite.h"
#include "game/world.h"
#include "game/inventory.h"
#include "game/menu.h"
#include "game/map_markers.h"
#include <map>
#include "game/high_scores.h"
#include "game/presentation.h"
#include "render/gfx.h"
#include "render/animation.h"
#include "render/menu_effects.h"
#include <array>
#include <string_view>
namespace vyruz {
// Original panel assets, drawn in fixed gameplay coordinates independently of camera.
class HudRenderer {
public:
    bool load(const ContentReader&,const std::string& world,std::string& error);
    void clear(Gfx&);
    void advance(double milliseconds,const GameWorld* world=nullptr);
    void setLevel(const Level&);
    void setLanguage(int language) { language_=language==2?2:1; }
    void rebuildMapMarkers(const SceneContent& scene,const GameWorld& world) { markers_.rebuild(scene,world.nativeActorCells());markersReady_=true; }
    void inheritInventory(HudRenderer& old) { heldIcons_=std::move(old.heldIcons_);heldBanks_=std::move(old.heldBanks_);old.heldIcons_.clear();old.heldBanks_.clear(); }
    void advanceInventory(double milliseconds,const GameWorld&,const InventoryView&);
    std::optional<AnimationCursorState> inventoryIconCursor(const HeldCode&) const;
    void syncInventory(const GameWorld&,const std::function<std::optional<AnimationCursorState>(size_t)>& seed={});
    void draw(Gfx&,const GameWorld&,const SceneContent&,bool paused,float width=640,float height=480);
    void drawSurpriseWorld(Gfx&,const GameWorld&,float cameraX,float cameraY);
    void drawSurpriseStatus(Gfx&,const GameWorld&,float width=640,float height=480);
    void drawMenu(Gfx&,const MenuView&,bool backgroundOnly=false);
    void drawControlsHelp(Gfx&,const std::vector<std::pair<std::string,int>>&,int first);
    unsigned advanceMenu(MenuPage page,uint32_t elapsedMs,int refreshHz,bool pointerVisible=false);
    void drawMenuScanline(Gfx&);
    void drawNameCaret(std::string_view label,float x,float y);
    void drawMenuCursor(Gfx&,float x,float y);
    float drawMenuText(Gfx&,std::string_view,float x,float y,unsigned font=4,float width=640,bool wrap=false);
    void drawHighScores(Gfx&,const HighScores&);
    void drawCursor(Gfx&,float x,float y);
    void drawPlayerIndicator(Gfx&,float x,float y,int heading);
    void drawNotifications(Gfx&,const GameWorld&,float cameraX,float cameraY);
    void drawDeathPrompt(Gfx&);
    void drawScreenBorders(Gfx&);
    void beginDemoOverlay();
    void advanceDemoOverlay(double milliseconds);
    void drawDemoOverlay(Gfx&);
    void drawPresentation(Gfx&,const Presentation&);
    void drawInventory(Gfx&,const GameWorld&,const InventoryView&,int width=640,int height=480,std::string_view closeKey="I");
private:
    int language_=1;
    ContentReader read_;
    MapMarkers markers_;bool markersReady_=false;
    std::array<AnimationCursor,26> panelCursors_;
    std::array<AnimationCursor,2> arrowCursors_;
    struct Bank { GafAnimation animation;std::vector<GLuint> textures; };
    struct HeldIcon { std::string path; AnimationCursor cursor; uint32_t sequence=0; uint64_t seen=0; };
    uint64_t inventorySyncSerial_=0;
    std::map<std::string,HeldIcon> heldIcons_;
    std::map<std::string,Bank> heldBanks_;
    static std::string heldIconId(const HeldCode&);
    Bank coins_;
    double coinsRemainingMs_=500;
    bool coinsHidden_=false;
    Bank score_,panel_,vPanel_,pause_,failed_,inventory_,menu_,vMenu_,cursor_,bonus_,surprise_,darkness_,surpriseFx_;
    float xtcPhase_=0,xtcY_=0;
    bool xtcActive_=false;
    MenuEffects menuEffects_;
    uint64_t saveLabelClock_=0,saveLabelStart_=0;
    int saveLabelSelection_=-1;
    std::string saveLabelText_;
    float menuTextWidth(std::string_view,unsigned font) const;
    AnimationCursor menuCursor_;
    Image levelMap_;
    GLuint mapTexture_=0;
    int mapWidth_=1,mapHeight_=1;
    std::array<Bank,9> weaponIcons_;
    std::array<AnimationCursor,9> iconCursors_;
    std::vector<GafFont> fonts_,inventoryFonts_,menuFonts_,bonusFonts_;
    void textBank(Gfx&,std::string_view,float,float,Bank&,const GafFont&,bool additive,bool uppercase,float maxWidth);
    void bitmap(Gfx&,Bank&,size_t,float,float,float clip=1);
    void sequence(Gfx&,Bank&,size_t,float,float,float clip=1,const GafSubframe* pose=nullptr);
    void text(Gfx&,std::string_view,float,float,size_t font=0,bool inventory=false,float maxWidth=10000);
};
}
