#include "game/menu.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
void check(bool b,const char* reason){if(!b){std::cerr<<reason<<'\n';std::exit(1);}}
int main(){
 bool paused=false;MenuView menu;menu.show(paused,true);check(paused&&menu.page==MenuPage::Main,"boot menu freezes world");
 menu.back(paused);check(menu.open(),"escape at title does not quit");
 check(menu.activate(paused)==MenuAction::NewGame,"title new game action");
 menu.move(2);check(menu.activate(paused)==MenuAction::Options,"title options action");
 menu.showOptions();check(menu.page==MenuPage::Options&&paused,"options preserve title pause");
 menu.optionTitle="Video";menu.optionLabels={"Resolution","Gamma","Back"};
 check(menu.title()=="Video"&&menu.entries().size()==3,"dynamic options rows");
 menu.back(paused);check(menu.page==MenuPage::Main&&paused,"options return to title");
 menu.move(3);menu.activate(paused);check(menu.page==MenuPage::Extras,"title extras page");
 check(menu.entries().size()==7&&menu.entries()[5].action==MenuAction::ControlsHelp&&menu.entries()[0].action==MenuAction::Scores&&menu.entries()[4].action==MenuAction::Demo,"original Extras actions");
 check(menu.activate(paused)==MenuAction::Scores&&menu.page==MenuPage::Extras,"scores dispatch retains caller");
 menu.slidesAvailable=false;check(menu.entries().size()==6&&menu.entries()[4].action==MenuAction::ControlsHelp&&menu.entries()[3].action==MenuAction::Demo,"missing optional slides omitted");menu.slidesAvailable=true;
 menu.back(paused);check(menu.page==MenuPage::Main,"extras return to title");
 menu.move(1);menu.activate(paused);check(menu.page==MenuPage::Confirm,"title quit confirmation");
 check(menu.activate(paused)==MenuAction::None&&menu.page==MenuPage::Main,"quit cancel defaults safely");
 menu.move(4);menu.activate(paused);menu.move(1);check(menu.activate(paused)==MenuAction::Quit,"confirmed quit action");
 menu.close(paused);check(!paused,"close restores simulation");
 paused=true;menu.show(paused);menu.activate(paused);check(paused&&!menu.open(),"resume preserves prior pause");
 paused=false;menu.show(paused);menu.move(-1);check(menu.selected==5,"release menu wraps to End game");
 check(menu.activate(paused)==MenuAction::None&&menu.page==MenuPage::Confirm,"end game requests confirmation");
 check(menu.activate(paused)==MenuAction::None&&menu.page==MenuPage::Game,"cancel returns to game");
 menu.move(2);menu.activate(paused);menu.move(1);check(menu.activate(paused)==MenuAction::Restart,"explicit restart");
 menu.back(paused);menu.move(4);menu.activate(paused);check(menu.page==MenuPage::Controls,"game controls page");
 check(!menu.hover(0,0),"outside click is ignored");
 check(menu.hover(menu.rowLeft()+1,menu.rowBaseline(int(menu.entries().size())-1)-29),"controls back hit target");menu.activate(paused);
 check(menu.page==MenuPage::Game,"controls returns to caller");
 menu.move(3);check(menu.activate(paused)==MenuAction::Options,"in-game options action");
 menu.showOptions();menu.optionTitle="Keyboard";menu.optionLabels.clear();
 for(int i=0;i<12;++i)menu.optionLabels.push_back("Binding "+std::to_string(i));
 menu.move(-1);check(menu.selected==11&&menu.firstRow()==10,"options keyboard wrapping follows pages");
 check(menu.hover(MenuView::Left+1,menu.rowTop()-29)&&menu.selected==10,"mouse selects visible page first row");
 check(!menu.hover(MenuView::Left+1,menu.rowTop()+31),"mouse ignores rows past final page");
 menu.move(1);menu.move(1);check(menu.selected==0&&menu.firstRow()==0,"options wrap back to first page");
 menu.back(paused);check(menu.page==MenuPage::Game&&paused,"options return to paused game menu");
 menu.back(paused);check(!paused&&!menu.open(),"escape resumes game");
 menu.show(paused,true);menu.move(1);check(menu.activate(paused)==MenuAction::LoadGame,"title load action");
 menu.saveLabels={"First save","Second save"};menu.saveHelp={"First details","Second details"};menu.showSavedSessions();
 check(menu.page==MenuPage::SavedSessions&&menu.selected==3,"saved sessions default Back after blank spacer");
 menu.move(1);check(menu.selected==0&&menu.selectedHelp()=="First details","save navigation wraps to first record");
 check(menu.hover(menu.rowLeft()+1,76)&&menu.selected==1&&menu.selectedHelp()=="Second details","hover chooses second saved record");
 check(menu.activate(paused)==MenuAction::LoadSelected&&menu.selectedSave==1,"title selects exact save");
 menu.back(paused);check(menu.page==MenuPage::Main&&paused,"save Back preserves title pause");
 menu.close(paused);menu.show(paused);menu.move(1);check(menu.activate(paused)==MenuAction::LoadGame,"in-game opens browser before confirming a record");
 menu.showSavedSessions();menu.move(1);check(menu.activate(paused)==MenuAction::None&&menu.page==MenuPage::Confirm,"in-game chosen save confirms progress replacement");
 menu.activate(paused);check(menu.page==MenuPage::SavedSessions&&paused,"cancel returns to saved sessions");
 menu.activate(paused);menu.move(1);check(menu.activate(paused)==MenuAction::LoadSelected,"confirmed selected save action");
 menu.close(paused);menu.showDebug(paused);check(!menu.open(),"debug disabled by default");menu.debugEnabled=true;menu.showDebug(paused);
 check(paused&&menu.page==MenuPage::Debug,"debug shortcut pauses world");
 menu.debugAI=false;menu.debugGod=true;
 check(menu.entries()[1].label=="Enemy AI: OFF"&&menu.entries()[2].label=="God mode: ON","debug toggle labels");
 menu.move(1);check(menu.activate(paused)==MenuAction::DebugAI,"AI action stays in menu");
 menu.move(1);check(menu.activate(paused)==MenuAction::DebugGod,"god action");
 menu.move(1);check(menu.activate(paused)==MenuAction::DebugAmmo,"ammo action");
 menu.move(1);check(menu.activate(paused)==MenuAction::DebugMoney,"money action");
 menu.move(-4);menu.activate(paused);
 check(menu.page==MenuPage::DebugLevels&&menu.entries().size()==25,"all shipped maps selectable");
 menu.move(-1);check(menu.selected==24&&menu.firstRow()==20,"level pages wrap");
 check(menu.activate(paused)==MenuAction::DebugSelectLevel,"level select action");
 check(menu.hover(MenuView::Left+1,menu.rowTop()-29)&&menu.selected==20,"level page mouse selection");
 menu.back(paused);check(menu.page==MenuPage::Debug,"level back to debug");
 menu.move(5);menu.activate(paused);
 check(menu.page==MenuPage::DebugSurprises&&menu.entries().size()==21,"all Surprises plus clear selectable");
 menu.move(-1);check(menu.selected==20&&menu.firstRow()==20,"Surprise pages wrap");
 check(menu.activate(paused)==MenuAction::DebugSelectSurprise,"Surprise selection action");
 menu.back(paused);check(menu.page==MenuPage::Debug,"Surprise back to debug");
 menu.back(paused);menu.back(paused);check(!paused&&!menu.open(),"debug restores original pause state");

 // The extra legacy save is scrollable without moving native Back below315.
 menu.close(paused);menu.show(paused,true);menu.saveLabels.clear();
 for(int i=0;i<8;++i)menu.saveLabels.push_back("Save "+std::to_string(i));
 menu.showSavedSessions();check(menu.firstRow()==0&&menu.rowBaseline(menu.selected)==315,"Back anchored with legacy overflow");
 menu.move(-1);check(menu.selected==7&&menu.firstRow()==1,"last save scrolls into native seven-row viewport");
 check(menu.visibleRows()==std::vector<int>({1,2,3,4,5,6,7,8}),"save viewport includes anchored Back");
 check(!menu.hover(menu.rowLeft()+1,321),"save hits outside panel ignored");
 check(menu.hover(menu.rowLeft()+1,251)&&menu.selected==7,"scrolled save mouse identity");
 menu.move(1);check(menu.selected==8&&menu.firstRow()==1&&menu.rowBaseline(8)==315,"Back retains viewport");
 check(menu.hover(menu.rowLeft()+1,286)&&menu.selected==8,"fixed Back mouse identity");
 menu.move(1);check(menu.selected==0&&menu.firstRow()==0,"save wrapping returns viewport to start");
 menu.page=MenuPage::Controls;menu.controlsHelp.clear();for(int i=0;i<20;++i)menu.controlsHelp+="Control\n";
 menu.selected=0;menu.move(19);check(menu.selected==19&&menu.firstRow()==13,"Controls follows selected row like saves");
 check(menu.rowLeft()==293&&menu.rowHeight()==35&&menu.rowBaseline(20)==315,"Controls shares save list area and fixed Back");
 check(menu.visibleRows()==std::vector<int>({13,14,15,16,17,18,19,20}),"Controls seven visible rows and Back");
 check(menu.hover(294,251)&&menu.selected==19,"Controls mouse identity matches visible row");
 check(!menu.hover(631,251),"list mouse rejects outside right border");
 menu.move(1);menu.activate(paused);check(menu.page!=MenuPage::Controls,"Controls Back closes");
 menu.page=MenuPage::Controls;menu.selected=0;menu.activate(paused);check(menu.page==MenuPage::Controls,"informational control row does not close");
 menu.page=MenuPage::Extras;check(menu.rowHeight()==30,"Extras uses compact spacing");
 menu.page=MenuPage::Game;check(menu.rowHeight()==45,"game spacing preserved");

}
