#pragma once
#include "io/disc_import.h"
#include "game/controller.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstring>

namespace vyruz {
// Asset-independent bootstrap UI: original fonts/art are not readable yet.
class FirstRunScreen {
public:
    explicit FirstRunScreen(SDL_Renderer* renderer):renderer_(renderer){}
    void draw(const ImportProgress& progress,const std::string& error={},bool stopping=false) {
        SDL_SetRenderDrawColor(renderer_,9,14,21,255);SDL_RenderClear(renderer_);
        color(105,235,155);text("VURUS",48,42,4);
        color(231,237,243);text(error.empty()?"PREPARING GAME FILES":"GAME FILES NOT READY",48,112,3);
        if(!error.empty()) {
            color(237,190,140);wrapped(error,48,172,58,8);
            color(38,52,65);rect(40,420,300,40);rect(400,420,200,40);
            color(183,196,207);text("ENTER / A: RETRY",48,432,2);text("ESC / B: EXIT",408,432,2);
        } else {
            color(183,196,207);text("FIRST RUN COPIES RESOURCES AND CD MUSIC.",48,166,2);
            text("YOUR ORIGINAL DISC IMAGE STAYS UNCHANGED.",48,190,2);
            const char* phase=progress.stage==ImportStage::Resources?"EXTRACTING GAME RESOURCES":
                progress.stage==ImportStage::Music?"RIPPING CD AUDIO":
                progress.stage==ImportStage::Finishing?"FINISHING IMPORT":"CHECKING GAME RESOURCES";
            color(105,235,155);text(stopping?"STOPPING SAFELY...":phase,48,238,2);
            color(38,52,65);rect(48,272,704,24);
            if(progress.total) {
                const double fraction=std::clamp(double(progress.completed)/progress.total,0.,1.);
                color(67,173,119);rect(48,272,int(704*fraction),24);
                color(231,237,243);text(std::to_string(int(100*fraction))+"% OF FILE",48,314,2);
            } else {
                color(67,173,119);rect(48+(SDL_GetTicks()/12)%600,272,104,24);
            }
            color(183,196,207);wrapped(progress.item,48,350,58,2);
            color(38,52,65);rect(40,420,stopping?520:240,40);
            color(150,167,183);text(stopping?"WAITING FOR PARTIAL FILE CLEANUP":"ESC / B: CANCEL",48,432,2);
        }
        SDL_RenderPresent(renderer_);
    }
private:
    SDL_Renderer* renderer_;
    void color(Uint8 r,Uint8 g,Uint8 b){SDL_SetRenderDrawColor(renderer_,r,g,b,255);}
    void rect(int x,int y,int w,int h){SDL_Rect r{x,y,w,h};SDL_RenderFillRect(renderer_,&r);}
    // Independently authored five-pixel glyphs; no game assets or system fonts.
    static std::array<unsigned char,7> glyph(char c) {
        if(c>='a'&&c<='z')c-=32;
        static constexpr std::array<std::array<unsigned char,7>,36> chars={{
            {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
            {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
            {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
            {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
            {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
            {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
            {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
            {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
            {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
            {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
            {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
            {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
            {14,17,17,15,1,1,14}
        }};
        if(c>='A'&&c<='Z')return chars[c-'A'];
        if(c>='0'&&c<='9')return chars[26+c-'0'];
        switch(c){case '.':return {0,0,0,0,0,6,6};case ':':return {0,6,6,0,6,6,0};
        case '-':return {0,0,0,31,0,0,0};case '/':return {1,2,2,4,8,8,16};
        case '%':return {25,25,2,4,8,19,19};case '(':return {2,4,8,8,8,4,2};
        case ')':return {8,4,2,2,2,4,8};case '_':return {0,0,0,0,0,0,31};
        case ',':return {0,0,0,0,6,6,4};case '!':return {4,4,4,4,4,0,4};
        case '\'':return {4,4,0,0,0,0,0};case ' ':return {};default:return {14,17,1,2,4,0,4};}
    }
    void text(const std::string& value,int x,int y,int scale){
        for(char c:value){
            const auto rows=glyph(c);
            for(int row=0;row<7;++row)for(int col=0;col<5;++col)
                if(rows[row]&(16>>col))rect(x+col*scale,y+row*scale,scale,scale);
            x+=6*scale;
        }
    }
    void wrapped(std::string value,int x,int y,size_t columns,int lines){
        for(int line=0;line<lines&&!value.empty();++line){size_t count=std::min(columns,value.size());
            if(count<value.size()){auto space=value.rfind(' ',count);if(space!=std::string::npos&&space>0)count=space;}
            text(value.substr(0,count),x,y,2);value.erase(0,count);while(!value.empty()&&value.front()==' ')value.erase(0,1);y+=26;
        }
    }
};

enum class FirstRunAction { None, Retry, Cancel };
class FirstRunController {
public:
    FirstRunController(){controller_.scan();blocked_=!controller_.controlsNeutral();}
    FirstRunAction event(const SDL_Event& e){
        if(controller_.event(e))blocked_=true;
        if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){focused_=false;blocked_=true;}
        if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){focused_=true;blocked_=true;}
        if(focused_&&blocked_&&controller_.controlsNeutral())blocked_=false;
        if(!focused_||blocked_||e.type!=SDL_CONTROLLERBUTTONDOWN||!controller_.owns(e.cbutton.which))return FirstRunAction::None;
        if(e.cbutton.button==SDL_CONTROLLER_BUTTON_B)return FirstRunAction::Cancel;
        if(e.cbutton.button==SDL_CONTROLLER_BUTTON_A)return FirstRunAction::Retry;
        return FirstRunAction::None;
    }
    void close(){controller_.close();}
private:
    ControllerInput controller_;
    bool focused_=true,blocked_=true;
};

inline bool prepareDataWithScreen(const std::string& input,const std::string& cacheRoot,
        DataLocation& result,std::string& error,const std::string& controllerMap={}) {
    constexpr Uint32 subsystems=SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER;
    SDL_SetMainReady();
    if(SDL_InitSubSystem(subsystems)!=0){error=SDL_GetError();return false;}
    if(!controllerMap.empty()&&SDL_GameControllerAddMappingsFromFile(controllerMap.c_str())<0){
        error=SDL_GetError();SDL_QuitSubSystem(subsystems);return false;
    }
    SDL_Window* window=SDL_CreateWindow("Vurus - preparing game files",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        800,480,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED):nullptr;
    if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){error=SDL_GetError();if(window)SDL_DestroyWindow(window);SDL_QuitSubSystem(subsystems);return false;}
    SDL_RenderSetLogicalSize(renderer,800,480);FirstRunScreen screen(renderer);
    const auto clicked=[&](const SDL_Event& e,int left,int right){
        if(e.type!=SDL_MOUSEBUTTONUP || e.button.button!=SDL_BUTTON_LEFT)return false;
        float x=0,y=0;SDL_RenderWindowToLogical(renderer,e.button.x,e.button.y,&x,&y);
        return x>=left&&x<right&&y>=420&&y<460;
    };
    FirstRunController controls;
    bool success=false,exit=false;
    do {
        std::atomic<bool> cancel=false,done=false;std::mutex mutex;ImportProgress state;
        screen.draw(state);
        std::thread worker([&]{success=prepareData(input,cacheRoot,result,error,[&](const ImportProgress& p){
            {std::lock_guard lock(mutex);state=p;}return !cancel.load();
        });done.store(true);});
        while(!done.load()) {
            SDL_Event event;while(SDL_PollEvent(&event)){
                const auto action=controls.event(event);
                if(event.type==SDL_QUIT || action==FirstRunAction::Cancel ||
                    (event.type==SDL_KEYDOWN&&event.key.keysym.sym==SDLK_ESCAPE)||clicked(event,40,280)){cancel=true;exit=true;}
            }
            ImportProgress p;{std::lock_guard lock(mutex);p=state;}screen.draw(p,{},cancel.load());SDL_Delay(16);
        }
        worker.join();
        if(cancel.load()){success=false;exit=true;}
        if(!success&&!exit){
            const char* driver=SDL_GetCurrentVideoDriver();
            if(driver&&(!std::strcmp(driver,"offscreen")||!std::strcmp(driver,"dummy")))exit=true;
            bool retry=false;
            while(!exit&&!retry){screen.draw({},error);SDL_Event event;
                if(SDL_WaitEventTimeout(&event,50)){
                    const auto action=controls.event(event);
                    if(event.type==SDL_QUIT || action==FirstRunAction::Cancel || (event.type==SDL_KEYDOWN&&event.key.keysym.sym==SDLK_ESCAPE)||clicked(event,400,600))exit=true;
                    else if(action==FirstRunAction::Retry || (event.type==SDL_KEYDOWN&&(event.key.keysym.sym==SDLK_RETURN||event.key.keysym.sym==SDLK_KP_ENTER))||clicked(event,40,340))retry=true;
                }
            }
        }
    }while(!success&&!exit);
    controls.close();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_QuitSubSystem(subsystems);
    return success;
}
}
