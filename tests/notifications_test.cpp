#include "game/notifications.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static void check(bool value,const char* why) {if(!value){std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    Notifications n;n.levelName("UNDERGROUND");n.mapDenied("Vyruz");
    n.add({NoticeKind::Reward,"1000 $",400,300,-1.5f,2000,0,true,true,2,5,0xf0ff10});
    check(n.entries()[2].visibleText().empty(),"native reveal starts blank");
    n.advance(49);check(n.entries()[2].visibleText().empty(),"50ms reveal boundary");
    n.advance(1);check(n.entries()[2].visibleText()=="1","first reward character");
    n.advance(250);check(n.entries()[2].visibleText()=="1000 $","completed reveal");
    check(std::abs(n.entries()[2].currentY()-(300-1.5*300/14))<.001,"native movement units");
    check(n.entries()[1].currentY()==240,"denial stationary");
    n.advance(1700);check(n.entries().size()==1,"2-second notices expire at boundary");
    check(n.entries()[0].text=="UNDERGROUND","5-second title remains");
    n.advance(3000);check(n.entries().empty(),"title expires");
    Notifications a,b;a.levelName("A");b.levelName("A");
    for(int i=0;i<60;++i)a.advance(1000.0/60);
    for(int i=0;i<90;++i)b.advance(1000.0/90);
    check(std::abs(a.entries()[0].currentY()-b.entries()[0].currentY())<.001,"presentation rate independence");
    auto y=a.entries()[0].currentY();a.advance(0);a.advance(-1);a.advance(NAN);
    check(y==a.entries()[0].currentY(),"invalid/paused time cannot advance");
    for(int i=0;i<300;++i)a.mapDenied("V");
    check(a.entries().size()==256,"bounded transient queue");a.clear();check(a.entries().empty(),"clear");
    a.saving("Saving in 5 seconds",3200.9f,2976.8f,500);
    const auto& saving=a.entries().front();
    check(saving.kind==NoticeKind::Saving && saving.worldSpace && !saving.reveal &&
          saving.font==6 && saving.mode==7 && saving.x==3050 && saving.y==2926,
          "native saving font and integer terminal anchor");
    a.advance(499);check(a.entries().size()==1,"countdown visible through499ms");
    a.saving("Saving in 4 seconds",3200,2976,500);
    check(a.entries().size()==1 && a.entries()[0].ageMs==0,"countdown refresh replaces prior glyphs");
    a.saving("Saving Completed.",3200,2976,3000);
    a.advance(2999);check(a.entries().size()==1,"completion lifetime");
    a.advance(1);check(a.entries().empty(),"completion expires at3s");
    a.savingPrice(7,10000,3200.9f,2976.8f);
    const auto& price=a.savingPrices().front();
    check(price.notice.text=="Save your game for 10000 $" && price.notice.x==3000 && price.notice.y==2876 &&
          price.notice.worldSpace && !price.notice.reveal && price.notice.font==6 && price.notice.mode==5,
          "price uses authored cost and original world label format");
    a.advanceSavingPrice(7,true,1000);check(!price.hidden && price.remainingMs==0,"price blink uses strict threshold");
    a.advanceSavingPrice(7,true,1);check(price.hidden && price.remainingMs==1000,"price whole-label blink");
    a.advanceSavingPrice(7,false,5000);check(!price.active && price.hidden && price.remainingMs==1000,"distant label freezes phase");
    a.advanceSavingPrice(7,true,1001);check(price.active && !price.hidden && price.remainingMs==1000,"reentry resumes blink and discards overshoot");
    a.clear();check(a.savingPrices().empty(),"scene reset clears price labels");
    a.greenInfo(NoticeKind::WeaponInfo,"WEAPON LASER",400,140,4000);
    check(a.entries().front().panelFont && a.entries().front().font==3 && a.entries().front().reveal,"green info uses PANEL font3 and reveal");
    a.advance(100);check(a.entries().front().visibleText()=="WE","green text native reveal");
    a.greenInfo(NoticeKind::Warning,"WARNING : YOU ARE LOCKED",100,90,500,true);
    a.advance(300);a.greenInfo(NoticeKind::Warning,"WARNING : YOU ARE LOCKED",100,90,500,true);
    check(a.entries().size()==2 && a.entries()[1].ageMs==300 && a.entries()[1].lifetimeMs==800,"lock refresh retains reveal progress");
    a.greenInfo(NoticeKind::WeaponInfo,"WEAPON SAW",400,140,4000);
    check(a.entries().size()==2 && a.entries().back().ageMs==0 && a.entries().back().text=="WEAPON SAW","new selection replaces coordinate slot");
    a.advance(500);check(a.entries().size()==1,"lock expires500ms after pursuit ends");
    a.clear();a.keyUsed(9,true);
    check(a.entries().size()==1&&a.entries()[0].text=="Using Code #9 !"&&a.entries()[0].visibleText()==a.entries()[0].text&&
          !a.entries()[0].worldSpace&&a.entries()[0].x==220&&a.entries()[0].y==140&&a.entries()[0].font==6&&a.entries()[0].mode==5,"native code-use centered font and immediate text");
    a.advance(1999);check(a.entries().size()==1,"access remains through1999ms");a.advance(1);check(a.entries().empty(),"access expires at2000ms");
    a.access(NoticeKind::AccessBlocked,"Gate needs key #3 !",100.9f,168.9f,true);a.advance(500);
    a.access(NoticeKind::AccessBlocked,"Gate needs key #3 !",100,168,true);
    check(a.entries().size()==1&&a.entries()[0].ageMs==0&&a.entries()[0].worldSpace&&a.entries()[0].color==0xffffff,"same-coordinate contact replaces and restarts native lifetime");
    a.access(NoticeKind::AccessBlocked,"Wait, lift will come.",150,168,true);
    check(a.entries().size()==2,"different native anchors remain distinct");

    a.clear();a.keyPicked(1,false,1,true);
    const auto& picked=a.entries().front();
    check(picked.text=="Key #1 !"&&picked.visibleText()==picked.text&&picked.color==0x00ff00&&
          !picked.worldSpace&&!picked.panelFont&&picked.font==6&&picked.mode==5&&
          picked.x==220&&picked.y==140&&picked.velocityY==0,"native green centered immediate key pickup");
    a.advance(1000);a.keyPicked(7,true,3,false);
    check(a.entries().size()==1&&a.entries()[0].text=="Code #7 : part 3 !"&&a.entries()[0].ageMs==0,"picked part number and coordinate replacement");
    a.keyPicked(7,true,4,true);
    check(a.entries().size()==1&&a.entries()[0].text=="Code #7 complete !","completion replaces segment notice");
    a.advance(1999);check(a.entries().size()==1,"pickup visible through1999ms");
    a.advance(1);check(a.entries().empty(),"pickup expires at2s");

    a.fastKill("Pilot",15000);
    check(a.entries().front().text=="Well done Pilot !!!\nFast Kill Bonus +15000 $"&&
          a.entries().front().color==0xff2010&&a.entries().front().alpha==255,"native Fast Kill text and initial color");
    a.advance(10);check(a.entries().front().pulseIntensity==0,"pulse strict timer comparison");
    a.advance(1);check(a.entries().front().pulseIntensity==4&&a.entries().front().alpha==4&&a.entries().front().mode==3,"native alpha pulse first step");
    for(int i=0;i<63;++i)a.advance(11);
    check(a.entries().front().pulseIntensity==248&&a.entries().front().pulseStep==-4&&a.entries().front().color==0xff2010,"pulse reverses at254 and preserves diffuse RGB");
    a.fastKill("Pilot",10000);check(a.entries().size()==1&&a.entries().front().ageMs==0,"new Fast Kill replaces same coordinate");
    a.advance(7999);check(a.entries().size()==1,"Fast Kill8s lifetime");a.advance(1);check(a.entries().empty(),"Fast Kill expiry");
    a.lastEnemy("Pilot");check(a.entries().front().text=="You are so professional Pilot !\nNo more enemy in this level !\n  Real Killer Bonus : +100000 $","native Real Killer wording");
    a.advance(10000);check(a.entries().empty(),"Real Killer10s lifetime");

    a.clear();a.setLanguage(2);
    for(int key=1;key<=10;++key) {
        a.keyPicked(key,false,1,true);check(a.entries().back().text=="Clef #"+std::to_string(key),"French ordinary key has native punctuation");
        a.keyPicked(key,true,2,false);check(a.entries().back().text=="Code #"+std::to_string(key)+" : partie 2 !","French code part");
        a.keyPicked(key,true,3,true);check(a.entries().back().text=="Code #"+std::to_string(key)+" complet !","French code completion");
        a.keyUsed(key,false);check(a.entries().back().text=="Activation Clef #"+std::to_string(key)+" !","French key use");
    }
    a.savingPrice(1,10000,0,0);a.setLanguage(1);check(a.savingPrices().back().notice.text=="Save your game for 10000 $","price language changes immediately without phase reset");
}
