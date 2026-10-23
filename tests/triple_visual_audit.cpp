// Original-resource regression for Triple Fire vehicle copies and muzzle flashes.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "game/triple_fire.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void need(bool v,const std::string& why){if(!v)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    need(argc==3,"usage: triple_visual_audit DATA_DIR OUTPUT_DIR");
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    need(SDL_Init(SDL_INIT_VIDEO)==0,"SDL");App a;
    a.window=SDL_CreateWindow("Triple Fire",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    need(a.window&&a.gfx.init(a.window),"GL");need(a.vfs.init(argv[1]),"data");
    need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);
    using namespace vyruz;
    // Keep authored classes/assets, but construct a valid Player-only arena.
    // Deactivating authored Spiders after loading into this smaller map leaves
    // navigation checkpoints referring to cells outside the synthetic arena.
    auto playerObject=a.content.objects.at(a.content.player->value);
    playerObject.id={0};playerObject.position={400,400};
    a.content.objects={playerObject};a.content.player=ObjectId{0};
    Level arena;arena.mapW=100;arena.mapH=100;arena.hasTerrainCollision=true;
    std::string error;need(a.world.load(a.content,arena,error),error);
    a.sceneRenderer.clear(a.gfx);
    need(a.sceneRenderer.load(a.content,[&](const auto& name,auto& bytes){return a.vfs.read(name,bytes);},error),error);
    a.world.setPlayerPosition(400,400);a.world.debugFillAmmo();
    auto initial=a.world.snapshot();
    auto& player=initial.actors[initial.player];
    const auto weapon=std::find_if(player.weapons.begin(),player.weapons.end(),[](const auto& w){return w.name=="Laser-W";});
    need(weapon!=player.weapons.end(),"single-barrel laser");player.selectedWeapon=weapon-player.weapons.begin();
    initial.surprise.kind=SurpriseKind::Triple;initial.surprise.applied=true;initial.surprise.remainingMs=20000;initial.surprise.tripleOffset=400;
    for(int heading=0;heading<24;++heading){
        need(a.world.restoreSnapshot(initial,error),error);a.world.setPlayerDirection(heading);
        WorldInput fire;fire.fire=true;a.world.tick(1.f/60,fire);
        const auto& shots=a.world.projectiles();need(shots.size()==3,"three single-barrel projectiles");
        const auto center=shots[2].position;
        for(int side=0;side<2;++side){const auto offset=tripleFireOffset(heading,400,side==0?1:-1);
            need(std::abs(shots[side].position.x-center.x-offset.x)<.002f&&std::abs(shots[side].position.y-center.y-offset.y)<.002f,"projectile side origin uses native surface units");}
    }
    need(a.world.restoreSnapshot(initial,error),error);a.world.setPlayerDirection(3);
    need(a.sceneRenderer.sync(a.content,error),error);
    auto actors=a.world.actors();for(auto& actor:actors){actor.fireRevision=0;actor.headlightVisible=false;}
    a.sceneRenderer.shadows=false;a.sceneRenderer.reactorFx=false;a.sceneRenderer.groundDetails=false;
    const auto base=a.sceneRenderer.capture();
    auto triple=initial.surprise;SurpriseState single;
    const auto render=[&](const char* name,const SurpriseState& bonus,float x,bool fire,bool visible=true){
        need(a.sceneRenderer.restore(base,error),error);
        actors[initial.player].alive=visible;actors[initial.player].fireRevision=fire?1:0;
        a.gfx.beginFrame(kVirtualW,kVirtualH);a.gfx.clear(.2f,.3f,.4f);
        a.sceneRenderer.draw(a.gfx,a.content,arena,80,160,x,400,3,-1,[](const ForegroundTile&){},&actors,nullptr,1,&bonus);
        std::vector<uint8_t> pixels(static_cast<size_t>(a.gfx.drawableWidth())*a.gfx.drawableHeight()*3);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
#if VYRUZ_GLES
        // ES guarantees RGBA/UNSIGNED_BYTE readback; normalize to the same RGB
        // samples used by the desktop opacity and muzzle assertions.
        std::vector<uint8_t> rgba(pixels.size()/3*4);
        glReadPixels(0,0,a.gfx.drawableWidth(),a.gfx.drawableHeight(),GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        for(size_t i=0;i<pixels.size()/3;++i)for(size_t c=0;c<3;++c)pixels[i*3+c]=rgba[i*4+c];
#else
        glReadPixels(0,0,a.gfx.drawableWidth(),a.gfx.drawableHeight(),GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
#endif
        need(glGetError()==GL_NO_ERROR,"read pixels");if(name)need(saveScreenshot((out/name).c_str(),a.gfx.drawableWidth(),a.gfx.drawableHeight()),"capture");return pixels;
    };
    const auto empty=render(nullptr,single,400,false,false);
    const auto center=render(nullptr,single,400,false);
    const auto offset=tripleFireOffset(3,400,1);
    need(std::abs(offset.x-50.5964f)<.001f&&offset.y==0,"original heading3 surface table constant");
    const auto right=render(nullptr,single,400+offset.x,false);
    const auto left=render(nullptr,single,400-offset.x,false);
    const auto copies=render("triple-body.ppm",triple,400,false);
    size_t halfPixels=0;
    for(size_t i=0;i<copies.size();i+=3){
        if(!std::equal(empty.begin()+i,empty.begin()+i+3,center.begin()+i)||!std::equal(empty.begin()+i,empty.begin()+i+3,left.begin()+i))continue;
        bool changed=false,half=true;
        for(size_t c=0;c<3;++c){changed|=std::abs(int(right[i+c])-int(empty[i+c]))>10;half&=std::abs(2*int(copies[i+c])-int(right[i+c])-int(empty[i+c]))<=3;}
        if(changed&&half)++halfPixels;
    }
    need(halfPixels>100,"native half-opacity copy body");
    const auto firing=render("triple-fire.ppm",triple,400,true);
    size_t copyFire[2]={};
    for(int side=0;side<2;++side){
        const auto body=side==0?left:right;
        const auto flash=render(nullptr,single,400+(side==0?-offset.x:offset.x),true);
        for(size_t i=0;i<flash.size();i+=3){
            if(std::equal(flash.begin()+i,flash.begin()+i+3,body.begin()+i))continue;
            if(!std::equal(firing.begin()+i,firing.begin()+i+3,copies.begin()+i))++copyFire[side];
        }
        need(copyFire[side]>20,"both ghost vehicles draw their firing effect");
    }
    std::ofstream report(out/"result.txt");report<<"PASS 24 projectile headings; half-opacity pixels="<<halfPixels<<"; copy muzzle pixels="<<copyFire[0]<<","<<copyFire[1]<<'\n';std::cout<<"PASS triple origins, opacity and both muzzle flashes\n";
    a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
