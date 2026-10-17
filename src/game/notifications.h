#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vyruz {
enum class NoticeKind { LevelName, MapDenied, Reward, ActorName, Pickup, Bonus, Saving, WeaponInfo, Warning, AccessBlocked, KeyUsed };
// 4312e0/4315f0: bitmap-font messages, distinct from persistent interaction prompts.
// Screen positions use the original 640x480 HUD space; world positions follow camera.
struct Notification {
    NoticeKind kind=NoticeKind::Reward;
    std::string text;
    float x=0,y=0,velocityY=0; // pixels per native 14ms unit
    double lifetimeMs=2000,ageMs=0;
    bool worldSpace=false,reveal=false;
    unsigned font=2,mode=5; // original one-based V-BONUS font and draw operation
    uint32_t color=0xffffff;
    bool panelFont=false; // PANEL.GAF green information font, not V-BONUS
    bool pulse=false;
    double pulseRemainingMs=10;
    int pulseIntensity=0,pulseStep=4,alpha=255;
    float currentY() const { return y+velocityY*static_cast<float>(ageMs/14.0); }
    std::string_view visibleText() const {
        return std::string_view(text).substr(0,reveal?std::min(text.size(),static_cast<size_t>(ageMs/50)):text.size());
    }
};
class Notifications {
public:
    void setLanguage(int language) {
        const int next=language==2?2:1;if(next==language_)return;
        for(auto& price:savingPrices_)price.notice.text=std::string(next==2?"Sauvez votre partie pour ":"Save your game for ")+std::to_string(price.cost)+" $";
        language_=next;
    }
    int language() const { return language_; }
    struct SavingPrice {
        size_t actor;
        Notification notice;
        bool active=false,hidden=false;
        double remainingMs=1000;
        int64_t cost=0;
    };
    const std::vector<SavingPrice>& savingPrices() const { return savingPrices_; }
    void savingPrice(size_t actor,int64_t cost,float x,float y) {
        Notification notice{NoticeKind::Saving,std::string(language_==2?"Sauvez votre partie pour ":"Save your game for ")+std::to_string(cost)+" $",
            std::trunc(x)-200,std::trunc(y)-100,0,99999999,0,true,false,6,5};
        savingPrices_.push_back({actor,std::move(notice),false,false,1000,cost});
    }
    void advanceSavingPrice(size_t actor,bool active,double ms) {
        for(auto& price:savingPrices_)if(price.actor==actor) {
            price.active=active;
            if(!active || !(ms>0) || !std::isfinite(ms))return;
            price.notice.ageMs+=ms;
            // 431530(1000,0)/4315f0: blink, strict comparison, discard overshoot.
            if(price.remainingMs<ms){price.hidden=!price.hidden;price.remainingMs=1000;}
            else price.remainingMs-=ms;
            return;
        }
    }
    const std::vector<Notification>& entries() const { return entries_; }
    void clear() { entries_.clear();savingPrices_.clear(); }
    void add(Notification value) {
        if(value.text.empty())return;
        // Bound presentation memory without affecting rewards or gameplay.
        if(entries_.size()==256)entries_.erase(entries_.begin());
        entries_.push_back(std::move(value));
    }
    void advance(double milliseconds) {
        if(!(milliseconds>0)||!std::isfinite(milliseconds))return;
        for(auto& entry:entries_) {
            entry.ageMs+=milliseconds;
            // 431530/431970: alpha triangle; font6's authored additive
            // software operation ignores alpha, so do not invent RGB pulsing.
            if(entry.pulse) {
                if(entry.pulseRemainingMs<milliseconds) {
                    int next=entry.pulseIntensity+entry.pulseStep;
                    if(next<0 || next>254)entry.pulseStep=-entry.pulseStep;
                    entry.pulseIntensity+=entry.pulseStep;entry.alpha=entry.pulseIntensity;
                    entry.mode=3;entry.pulseRemainingMs=10;
                } else entry.pulseRemainingMs-=milliseconds;
            }
        }
        std::erase_if(entries_,[](const auto& entry){return entry.ageMs>=entry.lifetimeMs;});
    }
    void greenInfo(NoticeKind kind,std::string text,float x,float y,double lifetime,bool refresh=false) {
        for(auto it=entries_.begin();it!=entries_.end();++it)if(it->panelFont && it->x==x && it->y==y) {
            if(refresh && it->text==text) {it->lifetimeMs=it->ageMs+lifetime;return;}
            entries_.erase(it);break;
        }
        add({kind,std::move(text),x,y,0,lifetime,0,false,true,3,1,0xffffff,true});
    }
    void access(NoticeKind kind,std::string text,float x,float y,bool worldSpace,uint32_t color=0xffffff) {
        // 4311a0 replacement mode1 removes prior notices at integer XY.
        x=std::trunc(x);y=std::trunc(y);
        std::erase_if(entries_,[&](const auto& n){return std::trunc(n.x)==x && std::trunc(n.currentY())==y;});
        add({kind,std::move(text),x,y,0,2000,0,worldSpace,false,6,5,color});
    }
    void keyPicked(int id,bool code,int part,bool complete) {
        // 444660: inventory is already updated before checking code completion.
        std::string text=std::string(code?"Code #":language_==2?"Clef #":"Key #")+std::to_string(id);
        if(code)text+=complete?(language_==2?" complet":" complete"):std::string(language_==2?" : partie ":" : part ")+std::to_string(part);
        if(code || language_!=2)text+=" !";
        access(NoticeKind::Pickup,text,220,140,false,0x00ff00);
    }
    void fastKill(const std::string& name,int64_t reward) {
        access(NoticeKind::Bonus,(language_==2?"Bien jou\xe9 ":"Well done ")+name+(language_==2?" !!!\nBonus de Tir Rapide +":" !!!\nFast Kill Bonus +")+std::to_string(reward)+" $",100,138,false,0xff2010);
        entries_.back().lifetimeMs=8000;entries_.back().pulse=true;
    }
    void lastEnemy(const std::string& name) {
        access(NoticeKind::Bonus,(language_==2?"Quel Professionnel ":"You are so professional ")+name+(language_==2?" !\nPlus aucun ennemis dans ce niveau !\n  Bonus pour Vrai Tueur : +100000 $":" !\nNo more enemy in this level !\n  Real Killer Bonus : +100000 $"),70,138,false);
        entries_.back().lifetimeMs=10000;entries_.back().mode=7;
    }
    void keyUsed(int id,bool code) {
        // 443500: viewport centre minus100 on both axes, in640x480 HUD space.
        access(NoticeKind::KeyUsed,std::string(language_==2?(code?"Activation Code #":"Activation Clef #"):(code?"Using Code #":"Using Key #"))+std::to_string(id)+" !",220,140,false);
    }
    void levelName(const std::string& name) {
        add({NoticeKind::LevelName,name,220,480,-1,5000,0,false,false,6,5,0xffe010});
    }
    void mapDenied(const std::string& playerName) {
        // 435fe0: the active player profile name from context+2754.
        add({NoticeKind::MapDenied,playerName+(language_==2?",\n   il faut PLUS d'argent\n     pour voir le Plan !":",\n   you need MORE Cash\n     to see the Map !"),120,240,0,2000,0,false,false,6,7});
    }
    void saving(std::string text,float towerX,float towerY,double lifetimeMs) {
        // 42a3b0/42a650: refresh one stationary world-space font6 notice.
        std::erase_if(entries_,[](const auto& n){return n.kind==NoticeKind::Saving;});
        add({NoticeKind::Saving,std::move(text),std::trunc(towerX)-150,
             std::trunc(towerY)-50,0,lifetimeMs,0,true,false,6,7});
    }
private:
    int language_=1;
    std::vector<Notification> entries_;
    std::vector<SavingPrice> savingPrices_;
};
}
