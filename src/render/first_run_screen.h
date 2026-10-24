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
    explicit FirstRunScreen(SDL_Renderer* renderer,const std::string& source={},const std::string& destination={})
        :renderer_(renderer),source_(source),destination_(destination),started_(SDL_GetTicks64()){}
    void draw(const ImportProgress& progress,const std::string& error={},bool stopping=false) {
        color(9,15,14);SDL_RenderClear(renderer_);
        gold();text("VURUS / FIRST RUN",24,32,2);
        color(5,9,8);rect(36,106,584,296); // Modal shadow.
        color(11,20,17);rect(28,98,584,296);
        frame(28,98,584,296);
        color(11,20,17);rect(154,96,332,20);
        gold();text(error.empty()?" COPY / IMPORT GAME FILES ":" GAME FILES NOT READY ",166,99,2);
        if(!error.empty()) {
            wrapped(error,48,140,44,8);
            button(40,420,300,"ENTER / A: RETRY");button(400,420,200,"ESC / B: EXIT");
        } else {
            const char* phase=progress.stage==ImportStage::Resources?"COPYING RESOURCES":
                progress.stage==ImportStage::Music?"COPYING CD MUSIC":
                progress.stage==ImportStage::Finishing?"VERIFYING / SAVING":"CHECKING GAME FILES";
            gold();text(stopping?"STOPPING SAFELY...":phase,48,130,2);
            text(shortened(progress.item.empty()?"LOOKING FOR ORIGINAL GAME DATA":progress.item,44),48,154,2);
            text("FROM: "+shortened(source_,38),48,181,2);
            text("TO:   "+shortened(destination_,38),48,203,2);
            bar("CURRENT TASK",progress.completed,progress.total,239,0);
            bar("OVERALL",progress.overallCompleted,progress.overallTotal,295,9);
            color(122,107,51);rect(32,345,576,1);
            gold();text("TIME: "+elapsed(),48,362,2);
            text(stopping?"CLEANING PARTIAL FILES":"ORIGINAL DISC UNCHANGED",258,362,2);
            button(40,420,stopping?560:240,stopping?"WAITING FOR SAFE CLEANUP":"ESC / B: CANCEL");
        }
        SDL_RenderPresent(renderer_);
    }
private:
    SDL_Renderer* renderer_;
    std::string source_,destination_;
    Uint64 started_;
    void color(Uint8 r,Uint8 g,Uint8 b){SDL_SetRenderDrawColor(renderer_,r,g,b,255);}
    void gold(){color(235,173,45);}
    void rect(int x,int y,int w,int h){SDL_Rect r{x,y,w,h};SDL_RenderFillRect(renderer_,&r);}
    void frame(int x,int y,int w,int h){
        color(122,107,51);
        for(int inset:{0,3}){SDL_Rect r{x+inset,y+inset,w-inset*2,h-inset*2};SDL_RenderDrawRect(renderer_,&r);}
    }
    void button(int x,int y,int w,const std::string& label){
        color(31,43,31);rect(x,y,w,38);gold();text(label,x+8,y+12,2);
    }
    static std::string shortened(const std::string& value,size_t columns){
        if(value.size()<=columns)return value;
        const auto left=(columns-3)/2;return value.substr(0,left)+"..."+value.substr(value.size()-(columns-3-left));
    }
    std::string elapsed()const{
        const auto seconds=(SDL_GetTicks64()-started_)/1000;
        const auto two=[](Uint64 n){return (n<10?"0":"")+std::to_string(n);};
        return two(seconds/3600)+":"+two(seconds/60%60)+":"+two(seconds%60);
    }
    void bar(const char* label,uint64_t done,uint64_t total,int y,int phase){
        gold();text(label,48,y-16,2);
        const double fraction=total?std::clamp(double(done)/total,0.,1.):0;
        text(total?std::to_string(int(fraction*100))+"%":"...",548,y+3,2);
        color(5,9,8);rect(48,y,482,20);
        for(int segment=0;segment<60;++segment){
            const int pulse=(int(SDL_GetTicks64()/70)+phase)%66;
            const bool filled=total?segment<int(fraction*60):(segment>=pulse-6&&segment<pulse);
            if(filled)gold();else color(47,58,33);
            // Three separated strokes per cell resemble DOS shaded blocks.
            for(int row=0;row<3;++row)rect(50+segment*8,y+3+row*5,5,3);
        }
    }
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
        case '\\':return {16,8,8,4,2,2,1};case '>':return {16,8,4,2,4,8,16};
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
        800,600,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED):nullptr;
    if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){error=SDL_GetError();if(window)SDL_DestroyWindow(window);SDL_QuitSubSystem(subsystems);return false;}
    SDL_RenderSetLogicalSize(renderer,640,480);FirstRunScreen screen(renderer,input,cacheRoot);
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
