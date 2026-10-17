#include "render/hud_layout.h"
#include "game/video_modes.h"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace vyruz;
static void need(bool ok){if(!ok){std::cerr<<"HUD layout check failed\n";std::exit(1);}}
int main(){
    std::set<std::pair<int,int>> modes(VideoModes.begin(),VideoModes.end());
    need(modes.size()==VideoModes.size());
    // Include custom/asymmetric/nonpreset and legacy small-window sizes.
    for(const auto size:{std::pair{640,480},{1080,1080},{1280,800},{5120,1440},{7680,4320},
                         {2098,1294},{1739,683},{400,300},{640,1000}})modes.insert(size);
    for(const auto& size:modes){
        const auto view=Viewport::fit(size.first,size.second);
        need(std::abs(view.width/view.height-float(size.first)/size.second)<.00001f);
        for(int mode=0;mode<int(HudAspects.size());++mode){
            const auto hud=HudLayout::fit(view,mode);
            need(hud.scale>0&&hud.width>=640&&hud.height>=480);
            need(hud.x>=-.001f&&hud.y>=-.001f);
            need(hud.x+hud.width*hud.scale<=view.width+.001f);
            need(hud.y+hud.height*hud.scale<=view.height+.001f);
            need(std::abs(hud.x*2+hud.width*hud.scale-view.width)<.001f);
            need(std::abs(hud.y*2+hud.height*hud.scale-view.height)<.001f);
            if(mode==0)need(hud.width==view.width&&hud.height==view.height&&hud.scale==1&&hud.x==0&&hud.y==0);
            else need(std::abs(hud.width/hud.height-float(HudAspects[mode].numerator)/HudAspects[mode].denominator)<.00001f);
            // Layout calculations never alter the world projection.
            const auto after=Viewport::fit(size.first,size.second);
            need(after.width==view.width&&after.height==view.height);
        }
    }
    for(const auto size:VideoModes)need(offeredVideoMode(size.first,size.second));
    need(!offeredVideoMode(0,0)&&!offeredVideoMode(5000,480)&&!offeredVideoMode(480,640));
    std::cout<<"PASS "<<modes.size()*HudAspects.size()<<" independent HUD layouts, preset inventory and unchanged world view\n";
}
